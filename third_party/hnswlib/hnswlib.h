// hnswlib.h — minimal HNSW implementation for in-process RAG use.
// API-compatible with the hnswlib 0.8.0 subset used by agent RAG:
//   InnerProductSpace, L2Space, HierarchicalNSW<float>
// Header-only, no external dependencies, C++17.
#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <queue>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hnswlib {

using labeltype = size_t;
using DISTFUNC  = float (*)(const void*, const void*, const void*);

// ---------------------------------------------------------------------------
// Space interfaces
// ---------------------------------------------------------------------------
class SpaceInterface {
public:
    virtual ~SpaceInterface() = default;
    virtual size_t   get_data_size()       = 0;
    virtual DISTFUNC get_dist_func()       = 0;
    virtual void*    get_dist_func_param() = 0;
};

class BaseFilterFunctor {
public:
    virtual ~BaseFilterFunctor() = default;
    virtual bool operator()(labeltype) = 0;
};

namespace detail {
    inline float ip_dist(const void* a, const void* b, const void* param) {
        const size_t n   = *static_cast<const size_t*>(param);
        const float* va  = static_cast<const float*>(a);
        const float* vb  = static_cast<const float*>(b);
        float dot = 0.f;
        for (size_t i = 0; i < n; ++i) dot += va[i] * vb[i];
        return 1.f - dot;   // distance = 1 - cosine (for normalised vectors)
    }

    inline float l2_dist(const void* a, const void* b, const void* param) {
        const size_t n  = *static_cast<const size_t*>(param);
        const float* va = static_cast<const float*>(a);
        const float* vb = static_cast<const float*>(b);
        float d = 0.f;
        for (size_t i = 0; i < n; ++i) { float diff = va[i] - vb[i]; d += diff * diff; }
        return d;
    }
}

class InnerProductSpace : public SpaceInterface {
    size_t dim_;
public:
    explicit InnerProductSpace(size_t dim) : dim_(dim) {}
    size_t   get_data_size()       override { return dim_ * sizeof(float); }
    DISTFUNC get_dist_func()       override { return detail::ip_dist; }
    void*    get_dist_func_param() override { return &dim_; }
};

class L2Space : public SpaceInterface {
    size_t dim_;
public:
    explicit L2Space(size_t dim) : dim_(dim) {}
    size_t   get_data_size()       override { return dim_ * sizeof(float); }
    DISTFUNC get_dist_func()       override { return detail::l2_dist; }
    void*    get_dist_func_param() override { return &dim_; }
};

// ---------------------------------------------------------------------------
// HierarchicalNSW<dist_t>
// ---------------------------------------------------------------------------
template<typename dist_t>
class HierarchicalNSW {
public:
    // ---- file format constants ----
    static constexpr uint32_t kMagic   = 0x484E5357u;  // 'HNSW'
    static constexpr uint32_t kVersion = 1u;

    // ---- constructor: build new index ----
    HierarchicalNSW(SpaceInterface* space,
                    size_t max_elements,
                    size_t M             = 16,
                    size_t ef_construction = 200,
                    size_t /*random_seed*/ = 100)
        : space_(space),
          M_(M), M0_(2 * M),
          ef_construction_(std::max(M, ef_construction)),
          ef_(10),
          max_elements_(max_elements),
          cur_count_(0),
          enterpoint_(0), max_level_(-1),
          dist_fn_(space->get_dist_func()),
          dist_param_(space->get_dist_func_param()),
          data_size_(space->get_data_size()),
          mult_(1.0 / std::log(static_cast<double>(M_))),
          level_rng_(42)
    {
        data_.reserve(max_elements);
        labels_.reserve(max_elements);
        levels_.reserve(max_elements);
        links_.reserve(max_elements);
    }

    // ---- constructor: load from file ----
    HierarchicalNSW(SpaceInterface* space,
                    const std::string& path,
                    bool  /*nmslib_compat*/ = false,
                    size_t /*hint_max*/     = 0)
        : space_(space),
          dist_fn_(space->get_dist_func()),
          dist_param_(space->get_dist_func_param()),
          data_size_(space->get_data_size()),
          level_rng_(42)
    {
        loadIndex(path, space);
    }

    // ---- info ----
    size_t getCurrentElementCount() const { return cur_count_; }
    size_t getMaxElements()         const { return max_elements_; }
    void   setEf(size_t ef)               { ef_ = ef; }

    // ---- resize ----
    void resizeIndex(size_t new_max) {
        max_elements_ = new_max;
        data_.reserve(new_max);
        labels_.reserve(new_max);
        levels_.reserve(new_max);
        links_.reserve(new_max);
    }

    // ---- lazy deletion ----
    void markDelete(labeltype label) {
        deleted_.insert(label);
    }
    bool isMarkedDeleted(labeltype label) const {
        return deleted_.count(label) > 0;
    }

    // ---- insert / update ----
    void addPoint(const void* data_point, labeltype label,
                  bool /*replace_deleted*/ = false)
    {
        // Update-in-place if label already exists.
        auto it = label_to_id_.find(label);
        if (it != label_to_id_.end()) {
            uint32_t id = it->second;
            std::memcpy(data_[id].data(), data_point, data_size_);
            deleted_.erase(label);
            return;
        }

        if (cur_count_ >= max_elements_) {
            resizeIndex(max_elements_ * 2 + 1024);
        }

        const uint32_t id = static_cast<uint32_t>(cur_count_++);
        label_to_id_[label] = id;

        // Store vector.
        std::vector<float> vec(data_size_ / sizeof(float));
        std::memcpy(vec.data(), data_point, data_size_);
        data_.push_back(std::move(vec));
        labels_.push_back(label);
        deleted_.erase(label);

        const int node_level = pickLevel();
        levels_.push_back(node_level);
        // Neighbour lists: one per level 0..node_level.
        links_.push_back(std::vector<std::vector<uint32_t>>(
            static_cast<size_t>(node_level) + 1));

        if (id == 0) {
            // First node — just set entry point.
            enterpoint_ = 0;
            max_level_  = node_level;
            return;
        }

        int enter     = static_cast<int>(enterpoint_);
        dist_t e_dist = distTo(id, enter);
        const int cur_max = max_level_;

        // Phase 1 — greedy descent to node_level+1.
        for (int lc = cur_max; lc > node_level; --lc) {
            bool changed = true;
            while (changed) {
                changed = false;
                for (uint32_t nb : safeLinks(enter, lc)) {
                    dist_t d = distTo(id, static_cast<int>(nb));
                    if (d < e_dist) { e_dist = d; enter = static_cast<int>(nb); changed = true; }
                }
            }
        }

        // Phase 2 — insert at each level 0..min(node_level, cur_max).
        for (int lc = std::min(node_level, cur_max); lc >= 0; --lc) {
            const size_t Mmax = (lc == 0) ? M0_ : M_;

            // Search ef_construction_ candidates at this level.
            auto cands = searchLayer(enter, data_[id].data(), ef_construction_, lc);
            auto selected = selectNeighbors(std::move(cands), M_);

            links_[id][static_cast<size_t>(lc)] = selected;

            // Back-links.
            for (uint32_t nb : selected) {
                auto& nb_links = links_[nb][static_cast<size_t>(lc)];
                if (nb_links.size() < Mmax) {
                    nb_links.push_back(id);
                } else {
                    // Shrink: replace worst neighbour if id is closer.
                    dist_t d_id    = distTo(nb, static_cast<int>(id));
                    dist_t d_worst = dist_t(0);
                    size_t wi      = 0;
                    for (size_t j = 0; j < nb_links.size(); ++j) {
                        dist_t dj = distTo(nb, static_cast<int>(nb_links[j]));
                        if (dj > d_worst) { d_worst = dj; wi = j; }
                    }
                    if (d_id < d_worst) nb_links[wi] = id;
                }
            }

            // Advance entry point for next level.
            if (!selected.empty()) enter = static_cast<int>(selected[0]);
        }

        if (node_level > cur_max) {
            enterpoint_ = id;
            max_level_  = node_level;
        }
    }

    // ---- search ----
    // Returns a max-heap of (distance, label), top() = farthest result.
    std::priority_queue<std::pair<dist_t, labeltype>>
    searchKnn(const void* query, size_t k,
              BaseFilterFunctor* /*filter*/ = nullptr) const
    {
        std::priority_queue<std::pair<dist_t, labeltype>> result;
        if (cur_count_ == 0) return result;

        int enter     = static_cast<int>(enterpoint_);
        dist_t e_dist = distRaw(query, enter);

        // Greedy descent from max_level_ to 1.
        for (int lc = max_level_; lc >= 1; --lc) {
            bool changed = true;
            while (changed) {
                changed = false;
                for (uint32_t nb : safeLinks(enter, lc)) {
                    dist_t d = distRaw(query, static_cast<int>(nb));
                    if (d < e_dist) { e_dist = d; enter = static_cast<int>(nb); changed = true; }
                }
            }
        }

        // Level-0 beam search with ef candidates.
        const size_t ef = std::max(ef_, k);
        auto cands = searchLayerRaw(query, enter, ef, 0);

        // Collect k nearest non-deleted, smallest distance first.
        std::vector<std::pair<dist_t, uint32_t>> sorted;
        sorted.reserve(cands.size());
        while (!cands.empty()) { sorted.push_back(cands.top()); cands.pop(); }
        std::sort(sorted.begin(), sorted.end());   // ascending distance

        for (const auto& [d, node_id] : sorted) {
            if (result.size() >= k) break;
            if (!isMarkedDeleted(labels_[node_id]))
                result.push({d, labels_[node_id]});
        }
        return result;
    }

    // ---- persistence ----
    void saveIndex(const std::string& path) const {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("HNSW saveIndex: cannot open " + path);

        auto w32 = [&](uint32_t v){ out.write(reinterpret_cast<const char*>(&v), 4); };
        auto w64 = [&](uint64_t v){ out.write(reinterpret_cast<const char*>(&v), 8); };

        w32(kMagic);
        w32(kVersion);
        w64(static_cast<uint64_t>(cur_count_));
        w64(static_cast<uint64_t>(M_));
        w64(static_cast<uint64_t>(ef_construction_));
        w64(static_cast<uint64_t>(ef_));
        w64(static_cast<uint64_t>(max_elements_));
        w32(static_cast<uint32_t>(enterpoint_));
        out.write(reinterpret_cast<const char*>(&max_level_), sizeof(int));
        w64(static_cast<uint64_t>(data_size_));

        for (size_t i = 0; i < cur_count_; ++i) {
            w64(static_cast<uint64_t>(labels_[i]));
            out.write(reinterpret_cast<const char*>(data_[i].data()),
                      static_cast<std::streamsize>(data_size_));
            out.write(reinterpret_cast<const char*>(&levels_[i]), sizeof(int));
            for (int lc = 0; lc <= levels_[i]; ++lc) {
                const auto& lv = links_[i][static_cast<size_t>(lc)];
                w32(static_cast<uint32_t>(lv.size()));
                for (uint32_t nb : lv) w32(nb);
            }
        }

        // Deleted labels.
        w64(static_cast<uint64_t>(deleted_.size()));
        for (labeltype lbl : deleted_) w64(static_cast<uint64_t>(lbl));
    }

    void loadIndex(const std::string& path, SpaceInterface* space,
                   size_t /*hint_max*/ = 0)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("HNSW loadIndex: cannot open " + path);

        auto r32 = [&]() -> uint32_t {
            uint32_t v = 0; in.read(reinterpret_cast<char*>(&v), 4); return v;
        };
        auto r64 = [&]() -> uint64_t {
            uint64_t v = 0; in.read(reinterpret_cast<char*>(&v), 8); return v;
        };

        if (r32() != kMagic)   throw std::runtime_error("HNSW loadIndex: bad magic");
        if (r32() != kVersion) throw std::runtime_error("HNSW loadIndex: unsupported version");

        cur_count_       = static_cast<size_t>(r64());
        M_               = static_cast<size_t>(r64());
        M0_              = 2 * M_;
        ef_construction_ = static_cast<size_t>(r64());
        ef_              = static_cast<size_t>(r64());
        max_elements_    = static_cast<size_t>(r64());
        enterpoint_      = r32();
        in.read(reinterpret_cast<char*>(&max_level_), sizeof(int));
        data_size_       = static_cast<size_t>(r64());
        mult_            = 1.0 / std::log(static_cast<double>(M_));
        dist_fn_         = space->get_dist_func();
        dist_param_      = space->get_dist_func_param();
        space_           = space;

        const size_t dim = data_size_ / sizeof(float);
        data_.resize(cur_count_, std::vector<float>(dim));
        labels_.resize(cur_count_);
        levels_.resize(cur_count_);
        links_.resize(cur_count_);

        for (size_t i = 0; i < cur_count_; ++i) {
            labels_[i] = static_cast<labeltype>(r64());
            label_to_id_[labels_[i]] = static_cast<uint32_t>(i);
            in.read(reinterpret_cast<char*>(data_[i].data()),
                    static_cast<std::streamsize>(data_size_));
            in.read(reinterpret_cast<char*>(&levels_[i]), sizeof(int));
            links_[i].resize(static_cast<size_t>(levels_[i]) + 1);
            for (int lc = 0; lc <= levels_[i]; ++lc) {
                const uint32_t nb_count = r32();
                links_[i][static_cast<size_t>(lc)].resize(nb_count);
                for (uint32_t& nb : links_[i][static_cast<size_t>(lc)]) nb = r32();
            }
        }

        const uint64_t del_count = r64();
        for (uint64_t j = 0; j < del_count; ++j)
            deleted_.insert(static_cast<labeltype>(r64()));
    }

private:
    SpaceInterface* space_       = nullptr;
    size_t          M_           = 16;
    size_t          M0_          = 32;
    size_t          ef_construction_ = 200;
    size_t          ef_          = 10;
    size_t          max_elements_= 0;
    size_t          cur_count_   = 0;
    uint32_t        enterpoint_  = 0;
    int             max_level_   = -1;
    double          mult_        = 1.0;
    size_t          data_size_   = 0;
    DISTFUNC        dist_fn_     = nullptr;
    void*           dist_param_  = nullptr;

    std::vector<std::vector<float>>                      data_;
    std::vector<labeltype>                               labels_;
    std::vector<int>                                     levels_;
    // links_[node][level] = list of neighbour node IDs
    std::vector<std::vector<std::vector<uint32_t>>>      links_;
    std::unordered_map<labeltype, uint32_t>              label_to_id_;
    std::unordered_set<labeltype>                        deleted_;
    mutable std::mt19937                                 level_rng_;

    // ---- helpers ----
    int pickLevel() {
        std::uniform_real_distribution<double> ud(0.0, 1.0);
        const double r = ud(level_rng_);
        return static_cast<int>(-std::log(r > 0.0 ? r : 1e-10) * mult_);
    }

    dist_t distTo(uint32_t a, int b) const {
        return static_cast<dist_t>(dist_fn_(
            data_[a].data(), data_[static_cast<size_t>(b)].data(), dist_param_));
    }
    dist_t distRaw(const void* q, int b) const {
        return static_cast<dist_t>(dist_fn_(
            q, data_[static_cast<size_t>(b)].data(), dist_param_));
    }

    // Returns neighbour list for node at level, or empty if level out of range.
    const std::vector<uint32_t>& safeLinks(int node, int level) const {
        static const std::vector<uint32_t> empty;
        if (node < 0 || static_cast<size_t>(node) >= links_.size()) return empty;
        const auto& lv = links_[static_cast<size_t>(node)];
        if (level < 0 || static_cast<size_t>(level) >= lv.size()) return empty;
        return lv[static_cast<size_t>(level)];
    }

    // Beam search at one level; returns max-heap(dist, node_id), size <= ef.
    std::priority_queue<std::pair<dist_t, uint32_t>>
    searchLayerRaw(const void* query, int enter_id, size_t ef, int level) const
    {
        std::unordered_set<uint32_t> visited;
        // max-heap — keeps ef worst
        std::priority_queue<std::pair<dist_t, uint32_t>> top_cands;
        // min-heap — next candidates to expand
        std::priority_queue<std::pair<dist_t, uint32_t>,
                            std::vector<std::pair<dist_t, uint32_t>>,
                            std::greater<std::pair<dist_t, uint32_t>>> work;

        const uint32_t enter_u = static_cast<uint32_t>(enter_id);
        const dist_t   d0      = distRaw(query, enter_id);
        top_cands.push({d0, enter_u});
        work.push({d0, enter_u});
        visited.insert(enter_u);

        while (!work.empty()) {
            const auto [cd, c] = work.top(); work.pop();

            // Early exit: best candidate is worse than the worst we already have.
            if (top_cands.size() >= ef && cd > top_cands.top().first) break;

            for (uint32_t nb : safeLinks(static_cast<int>(c), level)) {
                if (!visited.insert(nb).second) continue;
                const dist_t nd = distRaw(query, static_cast<int>(nb));
                if (top_cands.size() < ef || nd < top_cands.top().first) {
                    work.push({nd, nb});
                    top_cands.push({nd, nb});
                    if (top_cands.size() > ef) top_cands.pop();
                }
            }
        }
        return top_cands;
    }

    // Overload used during addPoint (query = existing node's vector).
    std::priority_queue<std::pair<dist_t, uint32_t>>
    searchLayer(int enter_id, const float* query_data, size_t ef, int level) const
    {
        return searchLayerRaw(query_data, enter_id, ef, level);
    }

    // Select the M nearest from a max-heap of (dist, node_id).
    std::vector<uint32_t> selectNeighbors(
        std::priority_queue<std::pair<dist_t, uint32_t>> cands, size_t M) const
    {
        std::vector<std::pair<dist_t, uint32_t>> sorted;
        sorted.reserve(cands.size());
        while (!cands.empty()) { sorted.push_back(cands.top()); cands.pop(); }
        std::sort(sorted.begin(), sorted.end());   // ascending distance
        const size_t take = std::min(M, sorted.size());
        std::vector<uint32_t> result;
        result.reserve(take);
        for (size_t i = 0; i < take; ++i) result.push_back(sorted[i].second);
        return result;
    }
};

}  // namespace hnswlib

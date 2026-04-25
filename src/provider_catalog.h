#pragma once

#include "storage.h"
#include "types.h"

#include <string>
#include <vector>

std::vector<ModelConfig> LoadProviderCatalog(AppStorage* storage, const ProviderConfig& provider, std::string* error);
bool LoadOllamaModelMetadata(const ProviderConfig& provider, const std::string& model_id, ModelConfig* model, std::string* error);
void SyncProviderModelsFromCatalog(ProviderConfig* provider, const std::vector<ModelConfig>& catalog_models);

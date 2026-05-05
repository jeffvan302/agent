import pathlib, sys

p = pathlib.Path('src/project_settings_dialog.cpp')
lines = p.read_text(encoding='utf-8').splitlines(keepends=True)

def insert_after_line(match_line, new_lines):
    for i, line in enumerate(lines):
        if match_line in line:
            for j, nl in enumerate(new_lines):
                lines.insert(i + 1 + j, nl)
            return True
    return False

# Member variables insertion
ok = insert_after_line('    HWND questionnaire_mode_combo_ = nullptr;', [
    '    HWND filesystem_enabled_check_ = nullptr;\n',
    '    HWND filesystem_auto_archive_check_ = nullptr;\n',
    '    HWND filesystem_workdir_label_ = nullptr;\n',
    '    HWND filesystem_workdir_edit_ = nullptr;\n',
    '    HWND filesystem_note_label_ = nullptr;\n',
    '\n',
    '    bool filesystem_enabled_ = false;\n',
    '    bool filesystem_auto_archive_ = false;\n',
    '    std::string filesystem_workdir_ = "$ProjectFolder$";\n',
])
print('insert member vars:', ok)

if not ok:
    print('Failed to insert member variables!')
    sys.exit(1)

p.write_text(''.join(lines), encoding='utf-8')
print('Done.')

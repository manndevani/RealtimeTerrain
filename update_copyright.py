import os
import glob

directory = r"G:\Epic Games\TerraDyne\Plugins\TerraDyne\Source"
extensions = ['*.cs', '*.cpp', '*.h']
files = []
for ext in extensions:
    files.extend(glob.glob(os.path.join(directory, '**', ext), recursive=True))

copyright_line = "// Copyright (c) 2026 GregOrigin. All Rights Reserved.\n"

for filepath in files:
    with open(filepath, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    
    if not lines:
        continue
        
    first_line = lines[0]
    
    if first_line.strip() == copyright_line.strip():
        continue
        
    if first_line.startswith('//'):
        lines[0] = copyright_line
    else:
        lines.insert(0, copyright_line)
        
    with open(filepath, 'w', encoding='utf-8') as f:
        f.writelines(lines)
    print(f"Updated {filepath}")

import os
import sys
from pathlib import Path

# Folders to skip (pruned so the script doesn't waste time recursing into them)
IGNORE_DIRS = {".git", "__pycache__", "node_modules", ".venv", "venv", ".idea", ".vscode", "build", "dist"}

# Only allow .hpp, .cpp, and .swift files
ALLOWED_EXTENSIONS = {".hpp", ".cpp", ".swift"}

def combine_files(folder_path=".", output_file="combined.txt"):
    folder = Path(folder_path).resolve()
    output_path = Path(output_file).resolve()

    count = 0
    with open(output_path, "w", encoding="utf-8") as outfile:
        # os.walk traverses all subdirectories recursively
        for root, dirs, files in os.walk(folder, topdown=True):
            # Prune ignored directories in-place so os.walk skips descending into them
            dirs[:] = [d for d in dirs if d not in IGNORE_DIRS]

            for file in sorted(files):
                file_path = Path(root) / file

                # Skip output file and non-matching extensions
                if file_path.resolve() == output_path:
                    continue
                if file_path.suffix.lower() not in ALLOWED_EXTENSIONS:
                    continue

                # Read file content safely
                try:
                    with open(file_path, "r", encoding="utf-8", errors="replace") as infile:
                        content = infile.read()
                except Exception as e:
                    print(f"Skipping {file_path.name} (error: {e})")
                    continue

                # Use relative path with forward slashes for consistent headers
                rel_path = file_path.relative_to(folder).as_posix()

                # Write formatted output
                outfile.write(f"[{rel_path}]\n")
                outfile.write(content.rstrip("\n") + "\n\n")
                count += 1

    print(f"Merged {count} files into: {output_path.name}")

if __name__ == "__main__":
    # Usage: python combine.py [folder_to_scan] [output_filename]
    source_folder = sys.argv[1] if len(sys.argv) > 1 else "."
    output_name = sys.argv[2] if len(sys.argv) > 2 else "combined.txt"

    combine_files(source_folder, output_name)
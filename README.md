# VCP - Versioned Copy Tool

A smart file copy utility that automatically maintains version history of overwritten files.

## Features

- **Automatic Versioning**: Every time a file is overwritten, the old version is preserved with a microsecond-precision timestamp
- **Organized Archive Structure**: Versions stored in hidden `.v` directories with intuitive folder hierarchy
- **Recursive Directory Support**: Copy entire directory trees while maintaining version history
- **Flat Mode**: Optional standard copy behavior without versioning (using `-f` flag)
- **Cross-Platform**: Works on Windows, Linux, and macOS
- **Smart Duplicate Detection**: Avoids unnecessary operations when source and destination are the same file
- **VCS Integration**: Automatically ignores `.git`, `.svn`, `.hg`, and `.v` directories

## Installation

### Build from Source

```bash
# Clone the repository
git clone https://github.com/yourusername/vcp.git
cd vcp

# Compile
g++ -o vcp vcp.cpp -std=c++11

# Optional: Install system-wide
sudo cp vcp /usr/local/bin/
```

### Windows

```cmd
g++ -o vcp vcp.cpp -std=c++11
```

## Usage

### Basic Syntax

```bash
vcp [SOURCE] [DESTINATION] [OPTIONS]
```

### Options

- `-f, --flat` - Flat mode: Standard overwrite behavior without version history

### Examples

#### Single File Copy

```bash
# Copy file.txt to destination (preserves old version if exists)
vcp file.txt /path/to/destination/

# Copy and rename
vcp file.txt /path/to/newname.txt
```

#### Directory Copy

```bash
# Copy entire directory recursively
vcp myproject/ /backup/

# Copy directory contents into existing directory
vcp src/ dest/
```

#### Flat Mode (No Versioning)

```bash
# Standard copy without version history
vcp -f file.txt destination/
vcp --flat mydir/ backup/
```

## Version History Structure

VCP stores archived versions in a hidden `.v` directory alongside your files:

```
your-directory/
├── file.txt (current version)
└── .v/
    └── file.txt/
        ├── 20260108-153021.123456/
        │   └── file.txt (version from Jan 8, 2026 15:30:21.123456)
        ├── 20260108-164530.789012/
        │   └── file.txt (version from Jan 8, 2026 16:45:30.789012)
        └── 20260109-091245.345678/
            └── file.txt (version from Jan 9, 2026 09:12:45.345678)
```

### Timestamp Format

Versions are stored with the format: `yyyyMMdd-HHmmss.uuuuuu`
- `yyyyMMdd` - Year, month, day
- `HHmmss` - Hour, minute, second (24-hour format)
- `uuuuuu` - Microseconds (ensures uniqueness for rapid successive copies)

## How It Works

1. **First Copy**: Creates the file at the destination
2. **Subsequent Copies**: 
   - Moves the existing file to `.v/filename/TIMESTAMP/filename`
   - Copies the new version to the destination
3. **Same File Detection**: If source and destination are the same file, creates a snapshot instead of overwriting
4. **Recursive Operations**: Applies the same versioning logic to all files in directory trees

## Use Cases

- **Development**: Never lose working code when experimenting
- **Document Editing**: Maintain automatic history of text files, configs, and scripts
- **Configuration Management**: Track changes to system or application config files
- **Backup Workflows**: Create timestamped backups automatically
- **Safe Overwrites**: Confidently copy files knowing you can recover previous versions

## Recovery

To restore a previous version:

```bash
# Navigate to the version history
cd .v/filename/

# List available versions
ls -l

# Copy the desired version back
vcp 20260108-153021.123456/filename ../filename -f
```

## Limitations

- Does not version the `.v` directory itself
- No automatic cleanup of old versions (manual management required)
- No compression (each version stores the full file)
- No content-based deduplication across versions

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

MIT License - See LICENSE file for details

## Author

Your Name (@yourusername)

## Acknowledgments

Inspired by the need for simple, automatic version control for everyday file operations.

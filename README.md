# rinse
NOTE: This readme is AI generated because i didn't want to sit down for sevral hours to do this. Small bits and pieces of the code for stuff i don't know how to do personally are also AI.
> Fast CLI frontend for pacman and AUR - Arch Linux package management made simple.

rinse is a lightweight terminal tool that wraps pacman and yay.

---

## 🚀 Quick Start

### Installation

```bash
curl -sSL https://raw.githubusercontent.com/rousevv/rinse/main/install.sh | bash
```

Or manually:
```bash
git clone https://github.com/rousevv/rinse.git
cd rinse
g++ -std=c++17 -O3 rinse.cpp -o rinse
sudo cp rinse /usr/local/bin/
```

### First Use

```bash
rinse firefox              # Install Firefox
rinse update               # Update your system
rinse --help               # See all commands
```

---

## 📦 Installing Packages

### Basic Install

```bash
rinse <package>            # Install a single package
rinse firefox              # Example: install Firefox
rinse install firefox      # Explicit install command (same thing)
```

### Multiple Packages

```bash
rinse firefox discord obsidian    # Install multiple at once
```

### How It Works

rinse automatically:
1. Checks if the package exists in **pacman** (official repos)
2. If not found, checks the **AUR**
3. Shows you the last update date
4. Asks for confirmation
5. Installs official packages first, then AUR packages
6. Sends a desktop notification when done (if enabled)

### Package Status Messages

When you try to install:

**New package:**
```
Installing package "firefox" from pacman
Last updated: 5th January 2026 (12 days ago)
[Y/n]
```

**Already installed (up to date):**
```
Package "firefox" already installed. Reinstall? [y/N]
```

**Already installed (outdated):**
```
Package "firefox" already installed, but outdated. Update? [Y/n]
```

**AUR package:**
```
Package "yay" not found on pacman, but found on the AUR.
Last updated: 3rd January 2026 (14 days ago)
[y/N]
```

**Not found:**
```
Package "nonexistent" not found on pacman or the AUR.
If the package is a .tar.gz file you want to install, run "rinse <path/to/file>"
```

### Installing from Files

```bash
rinse ./package.pkg.tar.zst    # Install from local pacman package
rinse ./app.tar.gz             # Extract to /opt/<app-name>
rinse /absolute/path/file.zst  # Works with absolute paths too
```

---

## 🔄 Updating Your System

### Update Everything

```bash
rinse update               # Updates pacman + AUR packages
rinse upgrade              # Alias for update
rinse new                  # Another alias
rinse -Syu                 # pacman-style syntax
```

### What Happens

1. Checks for outdated packages
2. Shows you how many are outdated
3. Updates official repos with `pacman -Syu`
4. Updates AUR packages with `yay -Syu` (if yay is installed)
5. Sends notification when complete

### Example Output

```
Found 157 outdated packages
Update all? [Y/n] y

Updating official packages...

Updating AUR packages...

✓ Update complete
```

---

## 🗑️ Removing Packages

### Basic Removal

```bash
rinse remove <package>     # Remove a package
rinse uninstall <package>  # Alias
rinse rem <package>        # Short alias
rinse -r <package>         # pacman-style
```

### Example

```bash
rinse remove neofetch
```

Output:
```
Remove package "neofetch"? [Y/n] y
Remove orphan dependencies? [Y/n] y
✓ Removal complete
```

### What It Does

1. Confirms you want to remove the package
2. Asks if you want to remove orphan dependencies (packages installed as dependencies but no longer needed)
3. Removes everything with a single pacman call

---

## 🔍 Searching Installed Packages

### List All Packages

```bash
rinse lookup               # List all installed packages
rinse check                # Alias
rinse -Q                   # pacman-style
```

### Fuzzy Search

```bash
rinse check firefox        # Find packages matching "firefox"
rinse check fire           # Partial match - finds "firefox"
rinse check fox            # Still finds "firefox"
```

### Search Multiple Terms

```bash
rinse check browser editor    # Find packages matching either term
```

### Example Output

```bash
$ rinse check fire
Found 1 package:
  firefox 122.0-1

$ rinse check browser
Found 3 packages:
  firefox 122.0-1
  chromium 121.0-1
  qutebrowser 3.0.0-1
```

**Features:**
- Case-insensitive search
- Substring matching (you don't need the exact name)
- Shows package name and version
- Returns nothing if no matches

---

## 🧹 Cleaning Up

### Clean Cache and Orphans

```bash
rinse clean
```

### What It Does

1. Cleans pacman package cache (`pacman -Sc`)
2. Cleans AUR cache if yay is installed (`yay -Sc`)
3. Asks if you want to remove orphan packages
4. Removes orphans if you confirm

### Example Output

```
Cleaning package cache...

Cleaning AUR cache...

Remove orphan packages? [Y/n] y
Removing orphan packages...

✓ Cache cleanup complete
```

---

## 📅 Finding Outdated Packages

### Check Stale Packages

```bash
rinse outdated             # Uses default threshold (6 months)
rinse outdated --time 1y   # Packages not updated in 1 year
rinse outdated --time 3m   # Packages not updated in 3 months
rinse outdated --time 90d  # Packages not updated in 90 days
```

### Time Format

- `d` = days (e.g., `30d`)
- `m` = months (e.g., `6m`)
- `y` = years (e.g., `2y`)

### What It Shows

```bash
$ rinse outdated --time 1y
Finding packages not updated in 365 days...
Found 5 outdated packages:
  gtk2 (last updated: 15 March 2021)
  lib32-gtk2 (last updated: 15 March 2021)
  python2 (last updated: 1 January 2020)
  ...
```

**Note:** This is informational only - it doesn't remove or update anything.

---

## 🧪 Dry Run Mode

### Preview Changes Without Executing

```bash
rinse --dry-run <command>  # Long form
rinse -n <command>         # Short form
rinse dry <command>        # Word form
```

### Examples

```bash
rinse -n firefox           # See what would happen
rinse --dry-run update     # Preview system update
rinse dry clean            # See what would be cleaned
```

### Output

```
[DRY RUN] Would execute: sudo pacman -S --noconfirm firefox
```

**Use this when:**
- You're testing rinse
- You want to see what packages would be affected
- You're writing scripts

---

## ⚙️ Configuration

### Config File Location

```
~/.config/rinse/rinse.conf
```

### Default Config

```bash
# rinse configuration file
# Lines starting with # are comments

# Keep build files after AUR installation
keep_build = false

# Send desktop notifications
notify = true

# Default time threshold for outdated packages
outdated_time = 6m
```

### Options Explained

**`keep_build`** (true/false)
- When installing AUR packages, keep the build directory
- Default: `false` (deletes build files after install)

**`notify`** (true/false)
- Send desktop notifications via `notify-send` when operations complete
- Default: `true`
- Requires `notify-send` to be installed

**`outdated_time`** (time value)
- Default threshold for `rinse outdated` command
- Format: `Nd` (days), `Nm` (months), `Ny` (years)
- Default: `6m` (6 months)

### Creating/Editing Config

The config file is created automatically on first run. To edit:

```bash
nano ~/.config/rinse/rinse.conf
```

---

## 🚩 Command-Line Flags

### Global Flags (work with any command)

**`--dry-run`, `-n`, `dry`**
- Preview what would happen without executing
- Shows commands that would be run

**`--full-log`**
- Show complete output from pacman/yay
- By default, rinse hides verbose output for cleaner UI
- Use this when debugging issues

**`-k`, `--keep`**
- Keep build files after AUR installation
- Overrides config setting for this operation

**`--time <value>`**
- Set time threshold for `outdated` command
- Example: `--time 2y`, `--time 90d`

**`--help`, `-h`, `-help`, `--h`, `help`**
- Show help message

### Examples

```bash
rinse -n firefox                    # Dry run install
rinse --full-log update             # See all pacman output
rinse -k install yay                # Keep yay build files
rinse outdated --time 2y            # 2-year threshold
```

---

## 🎯 Command Reference

### Install Commands

| Command | Description |
|---------|-------------|
| `rinse <pkg>` | Install package(s) |
| `rinse install <pkg>` | Install package(s) (explicit) |
| `rinse <file>` | Install from file |

### Update Commands

| Command | Description |
|---------|-------------|
| `rinse update` | Update all packages |
| `rinse upgrade` | Alias for update |
| `rinse new` | Alias for update |
| `rinse -Syu` | Update (pacman-style) |

### Remove Commands

| Command | Description |
|---------|-------------|
| `rinse remove <pkg>` | Remove package |
| `rinse uninstall <pkg>` | Alias for remove |
| `rinse rem <pkg>` | Short alias |
| `rinse -r <pkg>` | Remove (pacman-style) |

### Query Commands

| Command | Description |
|---------|-------------|
| `rinse lookup` | List all installed packages |
| `rinse lookup <term>` | Search installed packages |
| `rinse check` | Alias for lookup |
| `rinse -Q` | List (pacman-style) |

### Maintenance Commands

| Command | Description |
|---------|-------------|
| `rinse clean` | Clean cache & orphans |
| `rinse outdated` | Show stale packages |

---

## 💡 Tips & Tricks

### Combine with Other Tools

```bash
rinse check | grep python      # Find all Python packages
rinse outdated --time 2y > old.txt   # Save old packages to file
```

### Batch Operations

```bash
# Install your entire setup from a list
cat packages.txt | xargs rinse
```

### Check Before Update

```bash
rinse -n update                # See what would be updated
rinse update                   # Then actually update
```

### Find Zombie Packages

```bash
rinse outdated --time 3y       # Packages abandoned for 3+ years
```

---

## 🛠️ Troubleshooting

### "yay not found"

If you try to install an AUR package and yay isn't installed:
```
yay (AUR frontend) not found. Install? [Y/n]
```

rinse will offer to auto-install yay for you.

### Package Not Found

If rinse can't find a package in pacman or AUR:
```
Package "xyz" not found on pacman or the AUR.
If the package is a .tar.gz file you want to install, run "rinse <path/to/file>"
```

Double-check the package name on [archlinux.org/packages](https://archlinux.org/packages/) or [aur.archlinux.org](https://aur.archlinux.org/).

### Permission Errors

rinse uses `sudo` for system operations. Make sure your user is in the `wheel` group:
```bash
sudo usermod -aG wheel $USER
```

### See Full Output

If something fails and you need more details:
```bash
rinse --full-log update        # Shows all pacman output
```

---

## 🔧 Requirements

- **Arch Linux** (or Arch-based distro like Manjaro, EndeavourOS, CachyOS, etc.)
- **pacman** (pre-installed)
- **g++** with C++17 support (for building, not needed if you install the pre-compiled package)
- **yay** (optional, auto-installed when needed)
- **notify-send** (optional, for notifications)

---

## 🎨 Design Philosophy

rinse follows these principles:

1. **CLI-first** - No GUI, no TUI, just fast terminal commands
2. **Smart defaults** - Minimal config, works out of the box
3. **Safe operations** - Confirmations before system changes
4. **Clean output** - No verbose spam, just what you need
5. **Arch-idiomatic** - Respects pacman conventions
6. **Single binary** - One file, no dependencies

---

## 📝 Examples Gallery

### Daily Workflow

```bash
# Morning routine
rinse update                   # Update everything
rinse clean                    # Clean old packages

# Install new software
rinse obsidian discord         # Install apps
rinse check obsidian           # Verify it's installed

# Remove bloat
rinse outdated --time 1y       # Find old packages
rinse remove <old-package>     # Remove unwanted ones
```

### System Maintenance

```bash
# Monthly cleanup
rinse update
rinse clean
rinse outdated --time 6m
```

### Development Setup

```bash
# Install dev tools
rinse git base-devel vim neovim
rinse check dev                # Find all dev packages
```

---

## 🤝 Contributing

Found a bug? Want a feature? Open an issue or PR on [GitHub](https://github.com/rousevv/rinse).

---

also, you read the whole README.md, unless you just outright scrolled to the bottom, but here's a fun fact:
the r in rinse stands for rousev, being me, the ins stands for install and e because rinse is a real word and rins doesnt sound good, hence: rinse

**Made with ❤️ by Rousev for Arch Linux users who want a cleaner pacman workflow.**

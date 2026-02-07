# rinse
wrapper for pacman, the AUR and flatpak. (This project is currently a rough draft, please review code before using, and if you fix something, send a PR.)

## Installation

```bash
curl -sSL https://raw.githubusercontent.com/rousevv/rinse/main/install.sh | bash
```

Or manually:
```bash
git clone https://github.com/rousevv/rinse.git
cd rinse
g++ -std=c++17 -O3 rinse.cpp -o rinse
sudo cp rinse /usr/bin/
```
(the install.sh literally just automates these commands)

### First Use

```bash
rinse firefox              Installs firefox
rinse update               Update your system (updates pacman, AUR and flatpak apps.)
rinse --help
```

---

## Installing Packages

### Basic Install

```bash
rinse firefox
rinse install firefox
rinse -S firefox
...
```

### Multiple Packages

```bash
rinse firefox discord obsidian         Install multiple packages at once
```

### How It Works

rinse automatically checks if the package exists on the official repos, if not found, checks the AUR using yay (this is something i wanna phase out gradually, as i'd rather be using a native AUR handler, although it will probably not be anytime soon.) and sends a desktop notification when done (changable in config)

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

### Installing from Files - this currently doesnt work well.

```bash
rinse ./package.pkg.tar.zst    # Install from local pacman package
rinse ./app.tar.gz             # Extract to /opt/<app-name>
rinse /absolute/path/file.zst  # Works with absolute paths too
```

---

## Updating Your System

### Update Everything

```bash
rinse update
rinse upgrade
rinse new
rinse -Syu
```
all work.

### What it does

1. Checks for outdated packages on pacman, the AUR *AND* flatpak.
2. Shows you how many are outdated
3. Updates official repos with `pacman -Syu`
4. Updates AUR packages with `yay -Syu`
5. Updates flatpak apps with `flatpak update`
6. Sends notification when complete

### Example Output

```
Found 157 outdated packages
Update all? [Y/n] y

Updating official packages...

Updating AUR packages...

✓ Update complete
```

---

## Removing Packages

### Basic Removal

```bash
rinse remove <package>
rinse uninstall <package>
rinse rem <package>
rinse -r <package>
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

---

## Searching Installed Packages

### List All Packages

```bash
rinse lookup <package>
rinse check <package>
rinse -Q <package>
```

### Fuzzy Search

```bash
rinse check firefox        Find packages matching "firefox"
rinse check fire           Partial match, finds "firefox"
rinse check fox            Still finds "firefox"
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
```

**Features:**
- Case-insensitive search
- Substring matching (you don't need the exact name)
- Shows package name and version

---

## Cleaning Up

### Clean Cache and Orphans

```bash
rinse clean
```

### What It Does

1. Cleans pacman package cache (`pacman -Sc`)
2. Cleans AUR cache if yay is installed (`yay -Sc`)
3. Removes orphan packages

### Example Output

```
Cleaning package cache...

Cleaning AUR cache...

Remove orphan packages? [Y/n] y
Removing orphan packages...

✓ Cache cleanup complete
```

---

## Finding Outdated Packages - buggy right now

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

## Dry Run Mode

### Preview Changes Without Executing

```bash
rinse --dry-run <command>
rinse -n <command>
rinse dry <command>
```

### Examples

```bash
rinse -n firefox           See what would happen
rinse --dry-run update     Preview system update
rinse dry clean            See what would be cleaned (pun intended, i wouldve made it rinse clean dry, but that wouldn't make much sense syntax-wise)
```

### Output

```
[DRY RUN] Would execute: sudo pacman -S --noconfirm firefox
```

**Use this when:**
- You're testing
- You want to see what packages would be affected
- You're writing scripts (not reccomended to use this as a dependancy for a script, as it literally just stacks other dependencies in one, but feel free to ig)

---

## Configuration

### Config File Location

```
~/.config/rinse/rinse.conf
```

### Default Config

```bash
# rinse configuration file
# Save this to: ~/.config/rinse/rinse.conf
# Lines starting with # are comments and will be ignored

# ============================================
# BUILD SETTINGS
# ============================================

# Keep build files after AUR installation
# If true, build directories will be kept in /tmp for debugging
# If false (default), build files are automatically cleaned up
# Default: false
keep_build = false

# ============================================
# NOTIFICATION SETTINGS
# ============================================

# Send desktop notifications when operations complete
# Requires notify-send to be installed (usually part of libnotify)
# Notifications appear when packages are installed, updated, or removed
# Default: true
notify = true

# ============================================
# UPDATE SETTINGS
# ============================================

# Automatically check for rinse updates on 'rinse update'
# If true, rinse will check GitHub for new versions and offer to update itself
# If false, rinse will skip self-update checks entirely
# You can manually update by running the install script again
# Default: true
auto_update = true

# Branch to pull updates from
# Options: "main" (stable) or "experimental" (bleeding-edge features)
# Use "main" for stable, production-ready releases
# Use "experimental" to test new features before they're merged to main
# Default: main
update_branch = experimental

# ============================================
# PACKAGE MANAGEMENT SETTINGS
# ============================================

# Default time threshold for 'rinse outdated' command
# Format: Nd (days), Nm (months), Ny (years)
# Examples: 30d (30 days), 6m (6 months), 2y (2 years)
# This finds packages that haven't been updated upstream in the specified time
# Default: 6m
outdated_time = 6m
```

---

## Command-Line Flags

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
rinse -n firefox                    Dry run install
rinse --full-log update             See all output
rinse -k install fastfech           Keep yay build files
rinse outdated --time 2y            Check for packkages not updated in 2 years
```

---

## Command Reference

### Install Commands

|-------------------------------------------------------|
|    Command    |             Description               |
|-----------------------|-------------------------------|
| `rinse <pkg>`         | Install package(s)            |
| `rinse install <pkg>` | Install package(s) (explicit) |
| `rinse <file>`        | Install from file             |
|-------------------------------------------------------|
        
### Update Commands

|-----------------------------------------|
|     Command     |      Description      |
|-----------------|-----------------------|
| `rinse update`  | Update all packages   |
| `rinse upgrade` | Alias for update      |
| `rinse new`     | Alias for update      |
| `rinse -Syu`    | Update (pacman-style) |
|-----------------------------------------|

### Remove Commands

|-------------------------------------------------|
|         Command         |      Description      |
|-------------------------|-----------------------|
| `rinse remove <pkg>`    | Remove package        |
| `rinse uninstall <pkg>` | Alias for remove      |
| `rinse rem <pkg>`       | Short alias           |
| `rinse -r <pkg>`        | Remove (pacman-style) |
|-------------------------------------------------|

### Query Commands

|-----------------------------------------------------|
|        Command        |         Description         |
|-----------------------|-----------------------------|
| `rinse lookup`        | List all installed packages |
| `rinse lookup <term>` | Search installed packages   |
| `rinse check`         | Alias for lookup            |
| `rinse -Q`            | List (pacman-style)         |
|-----------------------------------------------------|

### Maintenance Commands

|-------------------------------------------|
|      Command      |      Description      |
|-------------------|-----------------------|
| `rinse clean`     | Clean cache & orphans |
| `rinse outdated`  | Show stale packages   |
|-------------------------------------------|

---

### Permission Errors

rinse uses `sudo` for system operations (like installing packages via pacman). Make sure your user is in the `wheel` group:
```bash
sudo usermod -aG wheel $USER
```

### See Full Output

If something fails and you need more details:
```bash
rinse --full-log update        # Shows all pacman output
```

---

## Requirements

- **Arch Linux** (or Arch-based distro like Manjaro, EndeavourOS, CachyOS, etc.)
- **pacman** (Arch's package manager)
- **g++** with C++17 support (for building, not needed if you install the pre-compiled package)
- **yay** (auto-installed when needed)
- **notify-send** (optional, for notifications)

---

## Contributing

Found a bug? Want a feature? Open an issue or PR!

---

also, you read the whole README.md, unless you just outright scrolled to the bottom, but here's a fun fact:
the r in rinse stands for rousev, being me, the ins stands for install and e because rinse is a real word and rins doesnt sound good, hence: rinse

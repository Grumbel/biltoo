{
  description = "Biltoo — classic Qt image viewer (Image, Gallery, Workspace)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    thumtoo.url = "github:Grumbel/thumtoo";
  };

  outputs = { self, nixpkgs, thumtoo }:
    let
      system = "x86_64-linux";
      # Default pkgs: no ccache overlay. Plain `nix build .#biltoo` must not
      # require a host shared cache or extra-sandbox-paths.
      pkgs = import nixpkgs { inherit system; };

      # ccacheStdenv's wrapper must not use $HOME/.ccache: under `nix build` the
      # sandbox sets HOME=/homeless-shelter (not writable) → "ccache: error:
      # Permission denied" on the first compiler probe. Point the wrapper at a
      # writable shared host dir (see .#ccache-check). Used only by
      # packages.biltoo.withCcache.
      ccacheWrapperExtraConfig = ''
        # Shared host cache only (no ephemeral fallback). HOME under
        # nix build is /homeless-shelter. Probe real write under dir/tmp.
        _biltoo_ccache_usable() {
          local d="$1"
          mkdir -p "$d/tmp" 2>/dev/null || return 1
          local probe="$d/tmp/.biltoo-write-test.$$"
          if ! ( : >"$probe" ) 2>/dev/null; then
            return 1
          fi
          rm -f "$probe" 2>/dev/null || true
          return 0
        }
        _chosen=""
        if [ -n "''${CCACHE_DIR:-}" ] && _biltoo_ccache_usable "$CCACHE_DIR"; then
          _chosen="$CCACHE_DIR"
        else
          for _cand in /var/cache/ccache /nix/var/cache/ccache; do
            if _biltoo_ccache_usable "$_cand"; then
              _chosen="$_cand"
              break
            fi
          done
        fi
        if [ -z "$_chosen" ]; then
          echo "ccache: FATAL — no writable shared CCACHE_DIR (no ephemeral fallback)" >&2
          echo "ccache: fix /var/cache/ccache perms + extra-sandbox-paths; nix run .#ccache-check" >&2
          echo "ccache: or build without ccache: nix build .#biltoo" >&2
          exit 1
        fi
        export CCACHE_DIR="$_chosen"
        export CCACHE_COMPRESS=1
      '';
      pkgsCcache = import nixpkgs {
        inherit system;
        overlays = [
          (final: prev: {
            ccacheWrapper = prev.ccacheWrapper.override {
              extraConfig = ccacheWrapperExtraConfig;
            };
          })
        ];
      };

      versionBase = nixpkgs.lib.strings.removeSuffix "\n" (builtins.readFile ./VERSION);
      gitRev = "${self.shortRev or self.dirtyShortRev or "dirty"}";
      isDev = nixpkgs.lib.strings.hasInfix "-dev" versionBase;
      version =
        if isDev then
          "${versionBase}.${toString (self.revCount or 0)}+g${gitRev}"
        else
          versionBase;

      biltooArgs = pkgsSet: {
        inherit version;
        kimageformats = pkgsSet.kdePackages.kimageformats;
        # Flake source of thumtoo (add_subdirectory in CMake; not a prebuilt package).
        thumtooSrc = thumtoo;
        # Same pkg-config deps as standalone thumtoo (libunarr, mupdf, …). Without
        # these, nested CMake configure silently disables optional backends.
        thumtooBuildInputs = thumtoo.lib.mkBuildInputs pkgsSet;
      };

      # Default package: stock stdenv, no ccache requirement.
      biltooPlain = pkgs.qt6Packages.callPackage ./default.nix (
        (biltooArgs pkgs) // {
          enableCcache = false;
        }
      );

      # Optional: ccacheStdenv + shared host CCACHE_DIR (extra-sandbox-paths).
      #   nix build .#biltoo.withCcache
      biltooWithCcache = pkgsCcache.qt6Packages.callPackage ./default.nix (
        (biltooArgs pkgsCcache) // {
          stdenv = pkgsCcache.ccacheStdenv;
          enableCcache = true;
        }
      );

      # Expose withCcache on the default attr path (passthru).
      biltoo = biltooPlain.overrideAttrs (old: {
        passthru = (old.passthru or { }) // {
          withCcache = biltooWithCcache;
        };
      });

      ccacheCheck = pkgs.writeShellScriptBin "biltoo-ccache-check" ''
        set -euo pipefail
        echo "biltoo-ccache-check — nix build ccache readiness"
        echo ""
        echo "What nix build needs for *persistent* hits across builds:"
        echo "  1) A host directory writable by the Nix builder user"
        echo "  2) That path listed in nix.conf extra-sandbox-paths"
        echo "  3) (optional) ownership so the builder can write"
        echo ""
        echo "Recommended:"
        echo "  sudo mkdir -p /var/cache/ccache"
        echo "  sudo chown \"$USER\":nixbld /var/cache/ccache   # or chmod 1777"
        echo "  # add to /etc/nix/nix.conf (or ~/.config/nix/nix.conf):"
        echo "  extra-sandbox-paths = /var/cache/ccache"
        echo "  # then restart the daemon: sudo systemctl restart nix-daemon"
        echo ""
        for cand in /var/cache/ccache /nix/var/cache/ccache; do
          printf "  %s: " "$cand"
          if [ ! -e "$cand" ]; then
            echo "missing"
          elif [ ! -d "$cand" ]; then
            echo "not a directory"
          elif mkdir -p "$cand/tmp" 2>/dev/null && ( : >"$cand/tmp/.biltoo-write-test.$$" ) 2>/dev/null; then
            rm -f "$cand/tmp/.biltoo-write-test.$$" 2>/dev/null || true
            echo "exists, writable OK (tmp/ create+write)"
          elif [ -w "$cand" ]; then
            echo "exists, dir -w but cannot write tmp/ (chown/chmod 2775 or 1777; fix tmp/ ownership)"
          else
            echo "exists, NOT writable (chown/chmod)"
          fi
        done
        echo ""
        echo "nix.conf extra-sandbox-paths:"
        if command -v nix >/dev/null 2>&1 && nix show-config >/dev/null 2>&1; then
          _esp=$(nix show-config 2>/dev/null | sed -n 's/^extra-sandbox-paths = //p' | head -n1 || true)
          if [ -z "$_esp" ]; then
            echo "  (empty — host cache will NOT be visible inside the sandbox)"
            echo "  → nix build will FAIL (no ephemeral fallback)"
          else
            echo "  $_esp"
            case " $_esp " in
              *"/var/cache/ccache"*|*" /nix/var/cache/ccache "*)
                echo "  → shared path listed; nix build should use host cache if writable"
                ;;
              *)
                echo "  → no /var/cache/ccache or /nix/var/cache/ccache entry"
                echo "  → add one and restart nix-daemon"
                ;;
            esac
          fi
        else
          echo "  (nix show-config not available)"
        fi
        echo ""
        echo "Without a writable shared path: nix build .#biltoo.withCcache FAILS (no ephemeral fallback)."
        echo "Plain nix build .#biltoo does not use ccache and needs no shared host dir."
        echo "With a writable shared path + extra-sandbox-paths: withCcache hits accumulate."
      '';

    in
    {
      packages.${system} = {
        default = biltoo;
        # Plain build (no ccache / no shared host cache required).
        #   nix build .#biltoo
        biltoo = biltoo;
        # Optional ccacheStdenv + shared host CCACHE_DIR:
        #   nix build .#biltoo.withCcache
        #   (also: packages.biltoo.withCcache / .#biltoo.withCcache)
        # Separate debug output from the package (ELF debuginfo under lib/debug).
        #   nix build .#debug
        #   gdb -ex "set debug-file-directory $(nix build --no-link --print-out-paths .#debug)/lib/debug" \
        #       $(nix build --no-link --print-out-paths)/bin/biltoo
        debug = biltoo.debug;
        # Diagnose persistent nix-build ccache for .#biltoo.withCcache
        # (extra-sandbox-paths + host dir).
        #   nix run .#ccache-check
        ccache-check = ccacheCheck;
      };

      apps.${system} = {
        default = {
          type = "app";
          program = "${biltoo}/bin/biltoo";
          meta = {
            description = "Biltoo — classic Qt image viewer (Image, Gallery, Workspace)";
          };
        };
        ccache-check = {
          type = "app";
          program = "${ccacheCheck}/bin/biltoo-ccache-check";
          meta = {
            description = "Check nix build ccache host dir + extra-sandbox-paths";
          };
        };
      };

      # `nix flake check` builds the package and runs CMake tests
      # (projectfile-roundtrip + biltoo --help; see default.nix doCheck).
      checks.${system} = {
        biltoo = biltoo;
      };

      # Local cmake builds default to Debug. nix build still uses RelWithDebInfo
      # with separateDebugInfo (see default.nix).
      #
      # Dev helpers are real PATH scripts (writeShellScriptBin), not shellHook
      # functions, so `nix develop -c biltoo-run` works (exec needs a binary).
      devShells.${system}.default =
        let
          # Recent nixpkgs dropped qtPluginPrefix on some Qt outputs; fall back
          # to the standard Qt 6 plugin layout under the lib output.
          qtPluginRoot = pkg:
            let
              libOut = pkgs.lib.getLib pkg;
              prefix = pkg.qtPluginPrefix or "lib/qt-6/plugins";
            in
            "${libOut}/${prefix}";
          qtPluginPath = pkgs.lib.concatStringsSep ":" [
            (qtPluginRoot pkgs.qt6.qtbase)
            (qtPluginRoot pkgs.qt6.qtsvg)
          ];

          # Shared preamble: BILTOO_SOURCE, build dir, and a *live* thumtoo tree.
          # Flake input / --override-input still evaluate to a /nix/store snapshot.
          # Pointing CMake at that snapshot means edits are invisible until
          # reconfigure, and each new store path forces a full thumtoo rebuild.
          biltooDevPreamble = ''
            set -euo pipefail
            if [ -z "''${BILTOO_SOURCE:-}" ]; then
              echo "$0: BILTOO_SOURCE is not set (enter the shell with: nix develop)" >&2
              exit 1
            fi
            BILTOO_BUILD_DIR="''${BILTOO_BUILD_DIR:-/tmp/biltoo-build}"

            # Canonical path for comparisons (strip trailing /; resolve . / ..).
            _biltoo_canon_path() {
              local p="$1"
              p="''${p%/}"
              if [ -d "$p" ]; then
                ( cd "$p" && pwd )
              else
                printf '%s\n' "$p"
              fi
            }
            _biltoo_resolve_thumtoo() {
              if [ -n "''${THUMTOO_SOURCE_DIR:-}" ] && [ -f "''${THUMTOO_SOURCE_DIR}/CMakeLists.txt" ]; then
                case "''${THUMTOO_SOURCE_DIR}" in
                  /nix/store/*)
                    echo "biltoo: THUMTOO_SOURCE_DIR is a Nix store path (frozen snapshot)." >&2
                    echo "  Local edits will not show up; reconfigure after each change full-rebuilds." >&2
                    echo "  Prefer a live checkout:" >&2
                    echo "    export THUMTOO_SOURCE_DIR=/path/to/thumtoo && biltoo-configure" >&2
                    ;;
                esac
                _biltoo_canon_path "''${THUMTOO_SOURCE_DIR}"
                return 0
              fi
              local cand
              for cand in \
                "''${BILTOO_SOURCE}/../thumtoo" \
                "''${BILTOO_SOURCE}/../thumtoo.git" \
                "''${BILTOO_SOURCE}/../thumtoo/thumtoo.git"
              do
                if [ -f "''${cand}/CMakeLists.txt" ]; then
                  _biltoo_canon_path "''${cand}"
                  return 0
                fi
              done
              printf '%s\n' "${thumtoo}"
            }
            export THUMTOO_SOURCE_DIR="$(_biltoo_resolve_thumtoo)"
          '';

          biltooConfigure = pkgs.writeShellScriptBin "biltoo-configure" (
            biltooDevPreamble
            + ''
              echo "biltoo-configure: THUMTOO_SOURCE_DIR=$THUMTOO_SOURCE_DIR"
              # Prefer ccache when the shell provides it (ccacheStdenv / packages).
              _ccache_args=()
              if command -v ccache >/dev/null 2>&1; then
                _ccache_args+=(
                  -DCMAKE_C_COMPILER_LAUNCHER=ccache
                  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
                )
              fi
              cmake -S "$BILTOO_SOURCE" -B "$BILTOO_BUILD_DIR" -G Ninja \
                -DCMAKE_BUILD_TYPE="''${CMAKE_BUILD_TYPE:-Debug}" \
                -DBILTOO_WITH_THUMTOO=ON \
                -DTHUMTOO_SOURCE_DIR="$THUMTOO_SOURCE_DIR" \
                "''${_ccache_args[@]}"
            ''
          );

          biltooBuild = pkgs.writeShellScriptBin "biltoo-build" (
            biltooDevPreamble
            + ''
              if [ ! -f "$BILTOO_BUILD_DIR/build.ninja" ] && [ ! -f "$BILTOO_BUILD_DIR/Makefile" ]; then
                biltoo-configure || exit 1
              fi

              # CMake bakes THUMTOO_SOURCE_DIR into the cache. If the resolved
              # path moved (new flake/override store hash, or switch to a live
              # checkout), rebuilds would keep compiling the *old* tree until
              # someone reconfigures. Detect mismatch and reconfigure once.
              cache="$BILTOO_BUILD_DIR/CMakeCache.txt"
              if [ -f "$cache" ]; then
                cached="$(sed -n 's/^THUMTOO_SOURCE_DIR:PATH=//p' "$cache" | head -n1 || true)"
                cached="$(_biltoo_canon_path "$cached")"
                if [ -n "$cached" ] && [ "$cached" != "$THUMTOO_SOURCE_DIR" ]; then
                  echo "biltoo-build: THUMTOO_SOURCE_DIR changed since configure:" >&2
                  echo "  cmake cache: $cached" >&2
                  echo "  current:     $THUMTOO_SOURCE_DIR" >&2
                  echo "  → re-running biltoo-configure (expect a thumtoo rebuild if the tree path differs)" >&2
                  biltoo-configure || exit 1
                elif [ -n "$cached" ] && [ ! -f "$cached/CMakeLists.txt" ]; then
                  echo "biltoo-build: cached THUMTOO_SOURCE_DIR is gone: $cached" >&2
                  echo "  → re-running biltoo-configure with $THUMTOO_SOURCE_DIR" >&2
                  biltoo-configure || exit 1
                fi
              fi

              cmake --build "$BILTOO_BUILD_DIR" "$@"
            ''
          );

          biltooRun = pkgs.writeShellScriptBin "biltoo-run" (
            biltooDevPreamble
            + ''
              biltoo-build || exit 1
              if [ ! -x "$BILTOO_BUILD_DIR/biltoo" ]; then
                echo "biltoo-run: $BILTOO_BUILD_DIR/biltoo missing after build" >&2
                exit 1
              fi
              # Do not use qtWrapperArgs here — those are makeWrapper flags.
              # shellHook / inputsFrom already put Qt plugins on QT_PLUGIN_PATH.
              # No exec: keep an interactive shell after biltoo exits when typed
              # by hand; under `nix develop -c` the process ends either way.
              "$BILTOO_BUILD_DIR/biltoo" "$@"
            ''
          );

          # Debug build + gdb. Extra args are biltoo's (via gdb --args).
          biltooRunGdb = pkgs.writeShellScriptBin "biltoo-run-gdb" (
            biltooDevPreamble
            + ''
              biltoo-build || exit 1
              if [ ! -x "$BILTOO_BUILD_DIR/biltoo" ]; then
                echo "biltoo-run-gdb: $BILTOO_BUILD_DIR/biltoo missing after build" >&2
                exit 1
              fi
              if ! command -v gdb >/dev/null 2>&1; then
                echo "biltoo-run-gdb: gdb not found (should be in the nix develop shell)" >&2
                exit 1
              fi
              # Inherit QT_PLUGIN_PATH / XDG_DATA_DIRS from shellHook.
              # No exec: return to the interactive shell when gdb exits.
              gdb --args "$BILTOO_BUILD_DIR/biltoo" "$@"
            ''
          );

          # Build + ctest. Extra args are forwarded to ctest (e.g. -R contentxform).
          biltooTest = pkgs.writeShellScriptBin "biltoo-test" (
            biltooDevPreamble
            + ''
              biltoo-build || exit 1
              if [ ! -f "$BILTOO_BUILD_DIR/CTestTestfile.cmake" ] \
                && [ ! -f "$BILTOO_BUILD_DIR/DartConfiguration.tcl" ]; then
                echo "biltoo-test: no CTest files in $BILTOO_BUILD_DIR (configure with tests?)" >&2
                exit 1
              fi
              # Match package doCheck: headless Qt + writable XDG cache for thumtoo.
              export QT_QPA_PLATFORM="''${QT_QPA_PLATFORM:-offscreen}"
              export XDG_CACHE_HOME="''${XDG_CACHE_HOME:-''${TMPDIR:-/tmp}/biltoo-test-cache}"
              mkdir -p "$XDG_CACHE_HOME"
              # ctest must run from the build tree so test properties resolve.
              cd "$BILTOO_BUILD_DIR"
              if [ "$#" -eq 0 ]; then
                ctest --output-on-failure
              else
                ctest --output-on-failure "$@"
              fi
            ''
          );
        in
        # ccacheStdenv: CC/CXX are ccache wrappers for out-of-tree cmake/ninja.
        pkgs.mkShell.override { stdenv = pkgs.ccacheStdenv; } {
          inputsFrom = [ biltoo ];
          packages = (with pkgs; [
            cmake
            ninja
            gdb
            ccache
            qt6.qttools
          ]) ++ [
            biltooConfigure
            biltooBuild
            biltooRun
            biltooRunGdb
            biltooTest
          ];
          CMAKE_BUILD_TYPE = "Debug";
          shellHook = ''
            # Source tree (stable even if someone cds away before biltoo-run).
            export BILTOO_SOURCE="$PWD"

            # Shared ccache dir for incremental biltoo-build (override with CCACHE_DIR).
            export CCACHE_DIR="''${CCACHE_DIR:-$HOME/.cache/ccache-biltoo}"
            mkdir -p "$CCACHE_DIR" 2>/dev/null || true
            # Also bake into cmake cache on (re)configure.
            export CMAKE_C_COMPILER_LAUNCHER=ccache
            export CMAKE_CXX_COMPILER_LAUNCHER=ccache

            # Theme search: FreeDesktop wants <datadir>/icons/hicolor/...
            # Our layout is data/icons/hicolor/... so datadir = $BILTOO_SOURCE/data.
            export XDG_DATA_DIRS="$BILTOO_SOURCE/data''${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"

            # Unwrapped out-of-tree biltoo does not get wrapQtAppsHook. Point Qt at
            # iconengines (svg) + imageformats from the same Qt the package uses.
            # (qtPluginPrefix is not always present on qtbase/qtsvg in current nixpkgs.)
            export QT_PLUGIN_PATH="${qtPluginPath}''${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"

            # Out-of-tree build dir (override with BILTOO_BUILD_DIR=...).
            export BILTOO_BUILD_DIR="''${BILTOO_BUILD_DIR:-/tmp/biltoo-build}"

            # Same live-tree resolution as biltoo-configure (see biltooDevPreamble).
            if [ -n "''${THUMTOO_SOURCE_DIR:-}" ] && [ -f "''${THUMTOO_SOURCE_DIR}/CMakeLists.txt" ]; then
              THUMTOO_SOURCE_DIR="$(cd "''${THUMTOO_SOURCE_DIR}" && pwd)"
            elif [ -z "''${THUMTOO_SOURCE_DIR:-}" ] || [ ! -f "''${THUMTOO_SOURCE_DIR}/CMakeLists.txt" ]; then
              for cand in \
                "$BILTOO_SOURCE/../thumtoo" \
                "$BILTOO_SOURCE/../thumtoo.git" \
                "$BILTOO_SOURCE/../thumtoo/thumtoo.git"
              do
                if [ -f "$cand/CMakeLists.txt" ]; then
                  THUMTOO_SOURCE_DIR="$(cd "$cand" && pwd)"
                  break
                fi
              done
            fi
            export THUMTOO_SOURCE_DIR="''${THUMTOO_SOURCE_DIR:-${thumtoo}}"
            case "$THUMTOO_SOURCE_DIR" in
              /nix/store/*)
                echo "note: THUMTOO_SOURCE_DIR is a store snapshot — export a live path for incremental thumtoo builds"
                ;;
            esac

            echo "biltoo dev shell (CMAKE_BUILD_TYPE=''${CMAKE_BUILD_TYPE:-Debug}, ccacheStdenv)"
            echo "  build dir: $BILTOO_BUILD_DIR"
            echo "  THUMTOO_SOURCE_DIR=$THUMTOO_SOURCE_DIR"
            echo "  version: cmake reads VERSION + .git (About → full 0.1.0-dev.N+gHASH)"
            echo "  biltoo-configure   # cmake once; then biltoo-build is incremental"
            echo "  biltoo-build       # incremental cmake --build (picks up thumtoo .cpp edits)"
            echo "  biltoo-run [args]  # build + run out-of-tree binary"
            echo "  biltoo-run-gdb [args]  # build + gdb --args biltoo"
            echo "  biltoo-test [ctest args]  # build + ctest (QT_QPA_PLATFORM=offscreen)"
            echo "  nix build .#biltoo            # RelWithDebInfo (no ccache)"
            echo "  nix build .#biltoo.withCcache  # same + shared-host ccache"
            echo "  nix build .#debug  # matching debug symbols"
            echo "  also: nix develop -c biltoo-run   # helpers are on PATH"
            echo ""
            echo "── ccache ──────────────────────────────────────────────"
            if ! command -v ccache >/dev/null 2>&1; then
              echo "  status:   ccache NOT on PATH (unexpected with ccacheStdenv)"
              echo "  fix:      re-enter the shell: nix develop"
            else
              echo "  binary:   $(command -v ccache)"
              echo "  version:  $(ccache --version 2>/dev/null | head -n1 || echo unknown)"
              echo "  CCACHE_DIR=$CCACHE_DIR"
              if [ -d "$CCACHE_DIR" ] && [ -w "$CCACHE_DIR" ]; then
                echo "  dir:      writable OK (biltoo-build / local ninja)"
              else
                echo "  dir:      NOT writable (or missing)"
                echo "  fix:      mkdir -p \"$CCACHE_DIR\" && chmod u+rwx \"$CCACHE_DIR\""
              fi
              echo "  CMAKE_C_COMPILER_LAUNCHER=$CMAKE_C_COMPILER_LAUNCHER"
              echo "  CMAKE_CXX_COMPILER_LAUNCHER=$CMAKE_CXX_COMPILER_LAUNCHER"
              # Resolve real compiler path (CC/CXX may be bare names).
              _cxx_path=""
              if [ -n "''${CXX:-}" ]; then
                _cxx_path=$(command -v "$CXX" 2>/dev/null || printf '%s' "$CXX")
              elif command -v c++ >/dev/null 2>&1; then
                _cxx_path=$(command -v c++)
              elif command -v g++ >/dev/null 2>&1; then
                _cxx_path=$(command -v g++)
              fi
              echo "  CXX path: ''${_cxx_path:-unknown}"
              case "$_cxx_path" in
                *ccache*) echo "  compiler: ccache-links wrapper (ccacheStdenv) OK" ;;
                *)
                  if [ "''${CMAKE_CXX_COMPILER_LAUNCHER:-}" = ccache ]; then
                    echo "  compiler: plain name + CMAKE_CXX_COMPILER_LAUNCHER=ccache"
                    echo "  path:     biltoo-build uses launcher (intended; not a failure)"
                  else
                    echo "  compiler: plain — set CMAKE_CXX_COMPILER_LAUNCHER=ccache"
                  fi
                  ;;
              esac
              if ccache -s >/tmp/biltoo-ccache-s.$$ 2>/dev/null; then
                echo "  stats (local biltoo-build cache):"
                if grep -E '^(Hits|Misses|Cache size|Files in cache|Primary|Uncacheable|Local storage)' /tmp/biltoo-ccache-s.$$ >/dev/null 2>&1; then
                  grep -E '^(Hits|Misses|Cache size|Files in cache|Primary|Uncacheable|Local storage|  Cache size)' /tmp/biltoo-ccache-s.$$ | head -n 12 | sed 's/^/    /'
                else
                  head -n 10 /tmp/biltoo-ccache-s.$$ | sed 's/^/    /'
                fi
                rm -f /tmp/biltoo-ccache-s.$$
              else
                echo "  stats:    (ccache -s failed — check CCACHE_DIR permissions)"
              fi
            fi
            echo "  nix build .#biltoo.withCcache (shared host only — no ephemeral fallback):"
            _nb_ok=
            for _cand in /var/cache/ccache /nix/var/cache/ccache; do
              if mkdir -p "$_cand/tmp" 2>/dev/null && ( : >"$_cand/tmp/.biltoo-write-test.$$" ) 2>/dev/null; then
                rm -f "$_cand/tmp/.biltoo-write-test.$$" 2>/dev/null || true
                _nb_ok="$_cand"
                break
              fi
            done
            if [ -n "$_nb_ok" ]; then
              echo "    host dir: $_nb_ok (writable probe OK)"
              echo "    expected log line: biltoo ccache: dir=$_nb_ok mode=shared-host"
              echo "    (requires extra-sandbox-paths for that dir)"
            else
              echo "    host dir: NONE writable — .#biltoo.withCcache will FAIL"
              echo "    plain:   nix build .#biltoo (no ccache, always OK)"
              echo "    setup:   nix run .#ccache-check"
            fi
            echo "────────────────────────────────────────────────────────"
          '';
        };
    };
}

{
  description = "Cloud Spanner Emulator developer environment";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
  };

  outputs = { self, nixpkgs }: let
    pkgs = import nixpkgs {
      system = "x86_64-linux";
      config.allowUnfree = true;
    };

    # nixpkgs only ships bazel_7 = 7.6.0, but .bazelversion pins 7.6.1.
    # Rather than rebuilding Bazel from source with an overridden src, we pin
    # the official 7.6.1 release binary from GitHub. It is dynamically linked
    # against glibc, which is why it is only ever run inside the FHS env below.
    bazelVersion = "7.6.1";
    bazel = pkgs.stdenvNoCC.mkDerivation {
      pname = "bazel";
      version = bazelVersion;

      src = pkgs.fetchurl {
        url = "https://github.com/bazelbuild/bazel/releases/download/${bazelVersion}/bazel-${bazelVersion}-linux-x86_64";
        hash = "sha256-rGJJ0Rkq6p/q9J3+4qtQw4zuJFSwDPKbvsmFoReVwCU=";
      };

      dontUnpack = true;
      dontStrip = true;
      dontPatchELF = true;

      installPhase = ''
        install -Dm755 $src $out/bin/bazel
      '';
    };

    fhs = pkgs.buildFHSEnv {
      name = "FHS";
      targetPkgs = pkgs: [
        pkgs.stdenv.cc.cc.lib
        pkgs.coreutils
        pkgs.claude-code

        # Bazel 7.6.1 (see above) and the toolchain the build expects, matching
        # build/docker/Dockerfile.ubuntu.
        bazel
        pkgs.gcc13  # Dockerfile pins GCC 13; GCC 15+ defaults to C23, which breaks the glib bundled in rules_foreign_cc's pkg-config bootstrap
        pkgs.binutils
        pkgs.jdk
        pkgs.python3
        pkgs.perl  # spanner_pg genrules run PostgreSQL's /usr/bin/perl scripts
        pkgs.git
        pkgs.gnumake
        pkgs.patch
        pkgs.which
        pkgs.curl
        pkgs.wget
        pkgs.gnutar
        pkgs.gzip
        pkgs.zip
        pkgs.unzip
        pkgs.zlib
        pkgs.gh
      ];
      multiPkgs = pkgs: [ ];
      runScript = "bash";
      profile = ''
        export LD_LIBRARY_PATH=${pkgs.stdenv.cc.cc.lib}/lib/:${pkgs.zlib}/lib/
        # .bazelrc forwards CC/CXX into the build via --action_env.
        export CC=gcc
        export CXX=g++
      '';
    };

  in

  {
    # Interactive shell: `nix develop`.
    devShells.x86_64-linux.default = fhs.env;
    # Non-interactive use: `nix run . -- -c 'bazel build //binaries:gateway_main'`.
    packages.x86_64-linux.default = fhs;
    packages.x86_64-linux.fhs = fhs;
    packages.x86_64-linux.bazel = bazel;
  };
}

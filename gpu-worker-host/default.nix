{ stdenv, callPackage, fetchFromGitHub, which, cmake, pkgsCross, libsodium, git, glibc, gcc, makeself, pkg-config }:
let
  rpclib = callPackage ../rpclib.nix {};
  #keystoneSdk = callPackage "${import ../keystoneSrc.nix}/sdk" {};
  keystoneSdk = callPackage ../../keystone/sdk {};

in
stdenv.mkDerivation {
  pname = "gpu-worker-host";
  version = "0.1"; # TODO

  src = ./.;

  preConfigure = ''
    cmakeFlagsArray+=(
      "-DKEYSTONE_SDK_DIR=${keystoneSdk}" "-DRPCLIB_ROOT=${rpclib}"
    )
  '';

  nativeBuildInputs = [
    cmake
    which
    pkgsCross.riscv64.buildPackages.gcc
    git
    makeself
    pkg-config
  ];

  buildInputs = [
    keystoneSdk
    rpclib
  ];

  installPhase = ''
    mkdir -p $out/bin
    cp gpu-worker-runner $out/bin/
  '';
}

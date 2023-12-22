{ stdenv, cmake, pkg-config, callPackage }: let

  rpclib = callPackage ../rpclib.nix {};

in
  stdenv.mkDerivation {
    pname = "gpu-worker-client";
    version = "0.0.1";

    src = ./.;

    nativeBuildInputs = [ cmake pkg-config rpclib ];

    installPhase = ''
      mkdir -p $out/bin
      cp gpu-worker-client $out/bin/
    '';
  }

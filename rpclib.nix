{ stdenv, cmake, fetchFromGitHub }:

stdenv.mkDerivation rec {
  pname = "rpclib";
  version = "2.3.0";

  src = fetchFromGitHub {
    owner = pname;
    repo = pname;
    rev = "v${version}";
    sha256 = "sha256-6RFFiJ/xMhJ5sSvmT67WKLtdeSh8JLt4n060fwidizY=";
  };

  nativeBuildInputs = [ cmake ];

  OUTPUT_LIBRARY_NAME = "rpclib";
}

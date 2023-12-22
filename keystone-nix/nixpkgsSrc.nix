# We pin a specific Nixpkgs revision here, to make the built image
# independent of the build host's Nixpkgs revision. Don't forget to
# update this periodically as well!
builtins.fetchGit {
  url = "https://github.com/NixOS/nixpkgs";
  ref = "nixos-23.11";
  rev = "781e2a9797ecf0f146e81425c822dca69fe4a348";
}

{ hostPackages, hostModulesPath, ... }:
{ config, pkgs, lib, modulesPath, ... }:

let
  # Avoid copying the entire keystone checkout to the store...
  #keystoneKmodSrc = "${import ../keystoneSrc.nix}/linux-keystone-driver";
  keystoneKmodSrc = ../../keystone/linux-keystone-driver;

  # Netboot currently has some assumptions about the target being
  # either x86 or aarch64. Patch this module to avoid adding grub2 &
  # syslinux for non-aarch64 systems:
  patchedNetboot = import (
    hostPackages.runCommand "patched-nixos-netboot" {} ''
      ${hostPackages.patch}/bin/patch -o $out ${
        # Using `modulesPath` here instead of `<nixos/...>` ensures that
        # we're using the modules of our desired Nixpkgs revision,
        # instead of the system-wide one on the build machine.
        hostModulesPath + "/installer/netboot/netboot.nix"} ${
          hostPackages.writeText "nixos-netboot.patch" (builtins.readFile
            ./0001-nixos-netboot-avoid-adding-grub2-syslinux-to-non-aar.patch)}
    ''
  );

in
{
  imports = [
    # This imports the `netboot` module, which defines some new
    # outputs, including an initramfs with all required Nix store
    # derivations packed into an embedded SquashFS.
    patchedNetboot

    # Avoid bloating our image by excluding not strictly necessary
    # derivations, such as manpages:
    (modulesPath + "/profiles/minimal.nix")
  ];

  nixpkgs.crossSystem.system = "riscv64-linux";

  # Some network configuration, this is not actually required for the
  # boot process itself.
  networking.hostName = "cos514-project";
  networking.useDHCP = true;
  networking.firewall.enable = false;

  # This avoids us having to embed a password hash or SSH key in the
  # configuration. Probably want to remove this on production systems.
  services.getty.autologinUser = lib.mkForce "root";

  # Let's add some useful utilities:
  environment.systemPackages = with pkgs; [
    vim tmux htop nload strace gdb
  ];

  # Always be sure to set `system.stateVersion`, if you don't want
  # Nixpkgs to yell at you!
  system.stateVersion = "23.05";

  boot.kernelParams = [ "console=ttyS0" "cma=1GB" ];

  boot.initrd.kernelModules = [
    "9p" "virtio" "9pnet_virtio" "virtio_net" "virtio_rng" "virtio_mmio" "keystone-driver"
  ];

  boot.extraModulePackages = [
    (config.boot.kernelPackages.callPackage keystoneKmodSrc {})
  ];

  # For 9pfs-mounted Nix store:
  #
  boot.initrd.postMountCommands = ''
    echo "Copying /nix-path-registration to /mnt-root/nix/store/"
    cat /nix-path-registration > /mnt-root/nix/store/nix-path-registration
  '';

  fileSystems."/nix/.ro-store" = lib.mkForce {
    device = "nixstore";
    fsType = "9p";
    options = [ "trans=virtio,msize=12582912" ];
    neededForBoot = true;
  };

  system.activationScripts.mountlocal9p = ''
    mkdir -p /root/local
    ${pkgs.mount}/bin/mount -t 9p local /root/local
  '';
}

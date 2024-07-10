{
  description = "Jhonatan's Nix env for C";
  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  inputs.nixvim.url = "github:nix-community/nixvim";
  inputs.nixpkgs.inputs.nixpkgs.url = "nixpkgs";

  outputs = { nixpkgs, nixvim, ... }:
	let 
		system = "x86_64-linux";
		pkgs = import nixpkgs {
			inherit system;
			config.allowUnfree = true;
		};
		packages = with pkgs; [libgcc gnumake valgrind-light zellij];
		nvim = nixvim.legacyPackages.${system}.makeNixvimWithModule {
			inherit pkgs;
			module = import ./nvim.nix;
		};
	in 
	{
		devShells.${system}.default = pkgs.mkShell {
			inherit system;
			name = "c-env";
			nativeBuildInputs = packages;
			buildInputs = [nvim];
		};
	};
}

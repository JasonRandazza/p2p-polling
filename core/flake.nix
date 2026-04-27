{
  description = "P2P Polling core module for Logos";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    logos-delivery.url = "path:/home/jrazz/logos-bootcamp/logos-delivery";
  };

  outputs = inputs@{ logos-module-builder, logos-delivery, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
      externalLibInputs = {
        logosdelivery = {
          input = logos-delivery;
          packages.default = "liblogosdelivery";
        };
      };
    };
}

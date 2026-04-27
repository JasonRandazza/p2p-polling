{
  description = "P2P Polling UI plugin for Logos Basecamp";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    polling_core.url = "path:../core";
  };

  outputs = inputs@{ logos-module-builder, polling_core, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}

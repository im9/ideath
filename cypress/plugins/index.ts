/// <reference types="cypress" />
// ***********************************************************
// This example plugins/index.js can be used to load plugins
//
// You can change the location of this file or turn off loading
// the plugins file with the 'pluginsFile' configuration option.
//
// You can read more here:
// https://on.cypress.io/plugins-guide
// ***********************************************************

// This function is called when a project is opened or re-opened (e.g. due to
// the project's config changing)
import { addMatchImageSnapshotPlugin } from "cypress-image-snapshot/plugin";

/**
 * @type {Cypress.PluginConfig}
 */
// eslint-disable-next-line no-unused-vars, import/no-anonymous-default-export
export default function (
  on: Cypress.PluginEvents,
  config: Cypress.PluginConfigOptions
): Cypress.PluginConfigOptions {
  addMatchImageSnapshotPlugin(on, config);
  return config;
}

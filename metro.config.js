/**
 * React Native 0.76 expects metro config to extend '@react-native/metro-config'.
 * We also point metro at the monorepo root so native/JS share one node_modules.
 */
const path = require('path');
const {getDefaultConfig, mergeConfig} = require('@react-native/metro-config');

const workspaceRoot = __dirname;
const projectRoot = path.join(workspaceRoot, 'mobile');

const defaultConfig = getDefaultConfig(projectRoot);

module.exports = mergeConfig(defaultConfig, {
  projectRoot,
  watchFolders: [workspaceRoot],
  resolver: {
    ...defaultConfig.resolver,
    nodeModulesPaths: [path.join(workspaceRoot, 'node_modules')],
    sourceExts: [...defaultConfig.resolver.sourceExts, 'cjs'],
  },
});

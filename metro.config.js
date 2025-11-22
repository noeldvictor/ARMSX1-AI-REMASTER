const path = require('path');

module.exports = {
  projectRoot: path.resolve(__dirname, 'mobile'),
  watchFolders: [__dirname],
  resolver: {
    sourceExts: ['js', 'jsx', 'json', 'ts', 'tsx'],
  },
};

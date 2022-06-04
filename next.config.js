const path = require("path");
const { createVanillaExtractPlugin } = require("@vanilla-extract/next-plugin");
const withVanillaExtract = createVanillaExtractPlugin();

/** @type {import('next').NextConfig} */
const nextConfig = {
  reactStrictMode: true,
  sassOptions: {
    // FIXME: 廃止予定
    includePaths: [path.join(__dirname, "styles")],
  },
  experimental: {
    optimizeFonts: true,
  },
  webpack: (config) => ({
    ...config,
    // NOTE: wasm を import する場合に使う
    // experiments: {
    //   asyncWebAssembly: true,
    // },
  }),
};

module.exports = withVanillaExtract(nextConfig);

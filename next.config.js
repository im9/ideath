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
};

module.exports = withVanillaExtract(nextConfig);

import {
  createGlobalThemeContract,
  createGlobalTheme,
  globalStyle,
} from "@vanilla-extract/css";

globalStyle("html, body", {
  width: "100%",
  height: "100%",
  minHeight: "100%",
  padding: "0",
  margin: "0",
  fontFamily:
    "Orbitron, -apple-system, BlinkMacSystemFont, Segoe UI, Roboto, Oxygen, Ubuntu, Cantarell, Fira Sans, Droid Sans, Helvetica Neue, sans-serif",
  backgroundColor: "var(--color-white)",
  fontSize: "14px",
});

globalStyle("#__next", {
  width: "100%",
  height: "100%",
});

globalStyle("a", {
  color: "inherit",
  textDecoration: "none",
});

globalStyle("*", {
  boxSizing: "border-box",
});

export const vars = createGlobalThemeContract({
  color: {
    white: "color-white",
    black: "color-black",
    gray: "color-gray",
    gray2: "color-gray2",
    gray3: "color-gray3",
    grayLight: "color-grayLight",
    grayLight2: "color-grayLight2",
    grayLight3: "color-grayLight3",
  },
});

// FIXME: 定数化
// #7fd8e4
createGlobalTheme(":root", vars, {
  color: {
    white: "#ffffff",
    black: "#333333",
    gray: "#636661",
    gray2: "#bebebe",
    gray3: "#cacaca",
    grayLight: "#e0e0e0",
    grayLight2: "#ededed",
    grayLight3: "f0f0f0", // 未
  },
});

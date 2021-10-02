import { style, globalStyle } from "@vanilla-extract/css";

export const parentClass = style({});

globalStyle("html, body", {
  padding: "0",
  margin: "0",
  fontFamily:
    "-apple-system, BlinkMacSystemFont, Segoe UI, Roboto, Oxygen, Ubuntu, Cantarell, Fira Sans, Droid Sans, Helvetica Neue, sans-serif",
  backgroundColor: "#e0e0e0",
});

globalStyle("a", {
  color: "inherit",
  textDecoration: "none",
});

globalStyle("*", {
  boxSizing: "border-box",
});

import { style } from "@vanilla-extract/css";

export const containerCls = style({
  height: "100%",
  backgroundColor: "var(--color-grayLight)",
});

export const mainCls = style({
  height: "100%",
});

export const btnAreaCls = style({
  position: "absolute",
  top: "10px",
  left: "20px",
  zIndex: 1,
  "@media": {
    "screen and (max-width: 480px)": {
      right: "10px",
    },
  },
});

// FIXME: TextButton コンポーネントとして切り出す
export const logoCls = style({
  background: "none",
  color: "var(--color-black)",
  border: "none",
  margin: "0.5rem auto",
  cursor: "pointer",
  position: "relative",
  display: "flex",
  alignItems: "center",
  textDecoration: "none",
  textTransform: "uppercase",
  transition: "0.2s",
  letterSpacing: "4px",
  overflow: "hidden",
  fill: "var(--color-black)",
  ":focus": {
    outline: "none",
  },
  ":hover": {
    color: "var(--color-gray)",
    fill: "var(--color-gray)",
  },
});

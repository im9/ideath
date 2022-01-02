import { style, globalStyle } from "@vanilla-extract/css";

export const containerCls = style({
  height: "100%",
  overflow: "hidden",
  backgroundColor: "var(--color-grayLight)",
});

export const mainCls = style({
  height: "100%",
});

export const titleCls = style({
  color: "var(--color-white)",
  fontSize: "4rem",
  fontWeight: 300,
  "@media": {
    "screen and (max-width: 480px)": {
      fontSize: "3rem",
    },
  },
});

export const heroImage = style({
  width: "100%",
  position: "relative",
  margin: "10% auto",
});

export const heroText = style({
  textAlign: "center",
  color: "var(--color-black)",
});

globalStyle(`${heroText} > h1`, {
  fontWeight: 300,
  "@media": {
    "screen and (max-width: 480px)": {
      fontSize: "1rem",
      margin: "4.5rem auto",
    },
  },
});

export const btnAreaCls = style({
  position: "absolute",
  top: "10px",
  left: "20px",
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

globalStyle(`${logoCls} > i`, {
  display: "flex",
  alignItems: "center",
});

export const seqArea = style({
  position: "relative",
  width: "100%",
  height: 150,
});

export const gearsSection = style({
  display: "flex",
  color: "var(--color-black)",
  fontWeight: 300,
  flexWrap: "wrap",
  "@media": {
    "screen and (max-width: 480px)": {
      fontSize: ".8rem",
    },
  },
});

export const gearsSectionDetail = style({
  margin: "20px",
});

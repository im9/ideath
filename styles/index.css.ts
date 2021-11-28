import { style, globalStyle } from "@vanilla-extract/css";

export const containerCls = style({
  height: "100%",
  background:
    "linear-gradient(225deg, var(--color-grayLight3), var(--color-gray3))",
  boxShadow: "-20px 20px 40px #d3d3d3, 20px -20px 40px var(--color-grayLight2)",
});

export const mainCls = style({
  height: "100%",
  background:
    "linear-gradient(225deg, var(--color-grayLight3), var(--color-gray3))",
  boxShadow: "-20px 20px 40px #d3d3d3, 20px -20px 40px var(--color-grayLight2)",
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
  backgroundImage:
    'linear-gradient(rgba(0, 0, 0, 0.5), rgba(0, 0, 0, 0.5)), url("/hero.jpg")',
  width: "100%",
  height: "100%",
  backgroundPosition: "center",
  backgroundRepeat: "no-repeat",
  backgroundSize: "cover",
  position: "relative",
  backgroundAttachment: "fixed",
  filter: "grayscale(.7)",
});

export const heroText = style({
  textAlign: "center",
  position: "absolute",
  top: "50%",
  left: "50%",
  width: "100%",
  transform: "translate(-50%, -50%)",
  color: "var(--color-white)",
  textShadow:
    "-20px 20px 40px #d3d3d3, 20px -20px 40px var(--color-grayLight2)",
});

export const btnAreaCls = style({
  position: "absolute",
  top: "0",
  right: "20px",
  "@media": {
    "screen and (max-width: 480px)": {
      right: "10px",
    },
  },
});

// FIXME: TextButton コンポーネントとして切り出す
export const loginBtnCls = style({
  background: "none",
  color: "var(--color-white)",
  border: "none",
  margin: "0.5rem auto",
  cursor: "pointer",
  position: "relative",
  fontSize: "1.2rem",
  display: "flex",
  alignItems: "center",
  textDecoration: "none",
  textTransform: "uppercase",
  transition: "0.2s",
  letterSpacing: "4px",
  overflow: "hidden",
  fill: "var(--color-white)",
  ":focus": {
    outline: "none",
  },
  ":hover": {
    color: "var(--color-gray)",
    fill: "var(--color-gray)",
  },
  "@media": {
    "screen and (max-width: 480px)": {
      fontSize: "1rem",
    },
  },
});

globalStyle(`${loginBtnCls} > i`, {
  display: "flex",
  alignItems: "center",
});

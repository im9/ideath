import { style } from "@vanilla-extract/css";

export const containerCls = style({
  minHeight: "100vh",
  padding: "0 0.5rem",
  display: "flex",
  flexDirection: "column",
  justifyContent: "center",
  alignItems: "center",
  height: "100vh",
  borderRadius: "20px",
  background:
    "linear-gradient(225deg, var(--color-grayLight3), var(--color-gray3))",
  boxShadow: "-20px 20px 40px #d3d3d3, 20px -20px 40px var(--color-grayLight2)",
});

export const mainCls = style({
  margin: "auto",
  padding: "1rem 2rem",
  borderRadius: "20px",
  background:
    "linear-gradient(225deg, var(--color-grayLight3), var(--color-gray3))",
  boxShadow: "-20px 20px 40px #d3d3d3, 20px -20px 40px var(--color-grayLight2)",
});

export const titleCls = style({
  color: "var(--color-gray)",
  fontWeight: 300,
});

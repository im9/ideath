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
});

export const mainCls = style({
  margin: "auto",
  padding: "1rem 2rem",
  borderRadius: "20px",
  background:
    "linear-gradient(225deg, var(--color-grayLight3), var(--color-gray3))",
  boxShadow: "4px 2px 16px rgba(136, 165, 191, 0.48), -4px -2px 16px #ffffff",
});

export const titleCls = style({
  color: "var(--color-gray)",
  fontWeight: 300,
});

import { style, globalStyle } from "@vanilla-extract/css";

export const containerCls = style({
  minHeight: "100vh",
  display: "flex",
  flexDirection: "column",
  justifyContent: "center",
  alignItems: "center",
  backgroundColor: "var(--color-grayLight)",
});

export const mainFrameCls = style({
  margin: "auto",
  padding: "1rem 2rem",
  // width: "90vw",
  borderRadius: "20px",
  background:
    "linear-gradient(225deg, var(--color-grayLight3), var(--color-gray3))",
  boxShadow: "-20px 20px 40px #d3d3d3, 20px -20px 40px var(--color-grayLight2)",
});

export const titleCls = style({
  color: "var(--color-gray)",
  fontWeight: 300,
});

export const stepBtnAreaLabel = style({
  display: "inline-block",
  fontSize: "0.5rem",
  color: "var(--color-gray)",
  background: "var(--color-white)",
  padding: "0 8px",
  borderRadius: "8px",
});

export const notes = style({
  flex: 1,
});

export const notesOptionArea = style({
  display: "flex",
  margin: "2rem 1rem",
  justifyContent: "space-between",
  gap: "8rem",
});

export const note = style({
  width: "64px",
  height: "64px",
  left: "0px",
  margin: "8px",
  background:
    "linear-gradient(225deg, var(--color-grayLight3), var(--color-gray3))",
  boxShadow: "4px 2px 16px rgba(136, 165, 191, 0.48), -4px -2px 16px #ffffff",
});

export const controlsCls = style({
  display: "flex",
  alignItems: "center",
  justifyContent: "space-between",
  marginTop: "1rem",
});

export const controlsDisplayCls = style({
  color: "var(--color-gray)",
  borderRadius: "20px",
  background: "var(--color-grayLight)",
  boxShadow:
    "inset -5px 5px 10px var(--color-gray2), inset 5px -5px 10px var(--color-white)",
  height: "110px",
  overflow: "scroll",
});

globalStyle(`${controlsDisplayCls} > dl`, {
  padding: "0 2rem",
});

globalStyle(`${controlsDisplayCls} > dl > dt`, {
  marginLeft: "1rem",
  width: "20px",
});

globalStyle(`${controlsDisplayCls} > dl > dd`, {
  margin: "0",
});

// label
globalStyle(`${controlsDisplayCls} > dl > dd > div:first-of-type`, {
  display: "inline",
  background: "var(--color-white)",
  fontWeight: "bold",
  padding: "0 8px",
  borderRadius: "8px",
});

globalStyle(`${controlsDisplayCls} > dl > dd > div:nth-of-type(2)`, {
  fontSize: " 0.8rem",
});

export const controlsDisplayParamAreaCls = style({
  display: "grid",
  gridTemplateColumns: "2fr 2fr 2fr 2fr 1fr",
  marginTop: "1rem",
});

export const padsCls = style({
  display: "flex",
});

// FIXME: 共通化
export const modalStyleCls = style({
  color: "var(--color-gray)",
  fontWeight: 300,
  backgroundColor: "var(--color-grayLight)",
  padding: "16px 40px 32px",
  borderRadius: "1rem",
  textAlign: "center",
  lineHeight: 1,
});

globalStyle(`${modalStyleCls} > p`, {
  margin: "2rem",
});

globalStyle(`${modalStyleCls} > footer`, {
  display: "flex",
  justifyContent: "space-evenly",
});

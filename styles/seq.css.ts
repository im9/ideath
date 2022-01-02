import { style, globalStyle } from "@vanilla-extract/css";

export const containerCls = style({
  minHeight: "100vh",
  display: "flex",
  flexDirection: "column",
  justifyContent: "center",
  alignItems: "center",
  backgroundColor: "var(--color-grayLight)",
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

export const settingsCls = style({
  display: "flex",
  alignItems: "flex-start",
  justifyContent: "flex-end",
});

export const settingsTrackCls = style({
  marginRight: "20px",
  flex: 1,
});

export const controlsCls = style({
  display: "flex",
  alignItems: "center",
  justifyContent: "space-between",
  marginTop: "1rem",
});

export const padsCls = style({
  display: "flex",
});

export const padsWrapperCls = style({
  marginTop: "1rem",
  display: "inline-block",
});

export const settingsTrackDisplayCls = style({
  color: "var(--color-gray)",
  borderRadius: "20px",
  background: "var(--color-grayLight)",
  boxShadow:
    "inset -5px 5px 10px var(--color-gray2), inset 5px -5px 10px var(--color-white)",
  height: "110px",
});

globalStyle(`${settingsTrackDisplayCls} > dl`, {
  display: "inline-flex",
});

globalStyle(`${settingsTrackDisplayCls} > dl > dt`, {
  marginLeft: "1rem",
  width: "20px",
});

globalStyle(`${settingsTrackDisplayCls} > dl > dd`, {
  margin: "0",
});

// label
globalStyle(`${settingsTrackDisplayCls} > dl > dd > div:first-of-type`, {
  display: "inline",
  background: "var(--color-white)",
  fontWeight: "bold",
  padding: "0 8px",
  borderRadius: "8px",
});

globalStyle(`${settingsTrackDisplayCls} > dl > dd > div:nth-of-type(2)`, {
  fontSize: " 0.8rem",
});

export const settingsTrackDisplayEffectAreaCls = style({
  marginTop: ".8rem",
  fontSize: " 0.8rem",
});

export const settingsBpmCls = style({
  display: "grid",
  margin: "0 0.8rem 0 2rem",
});

export const settingsTrackButtonAreaCls = style({
  display: "block",
});

// label
globalStyle(`${settingsTrackButtonAreaCls} > div:first-of-type`, {
  display: "inline-block",
  fontSize: "0.5rem",
  color: "var(--color-gray)",
  background: "var(--color-white)",
  padding: "0 8px",
  borderRadius: "8px",
});

export const settingsAreaCls = style({
  display: "flex",
  width: "76%",
  justifyContent: "space-between",
});

export const settingsTrackKnobAreaCls = style({
  display: "inline-flex",
});

globalStyle(`${settingsTrackKnobAreaCls} > div`, {
  margin: "0 0.5rem",
});

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

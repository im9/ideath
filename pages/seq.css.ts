import { style, globalStyle } from "@vanilla-extract/css";

export const containerCls = style({
  minHeight: "100vh",
  display: "flex",
  flexDirection: "column",
  justifyContent: "center",
  alignItems: "center",
});

export const mainCls = style({
  margin: "auto",
  padding: "1rem 2rem",
  borderRadius: "20px",
  background: "linear-gradient(225deg, #f0f0f0, #cacaca)",
  boxShadow: "-20px 20px 40px #d3d3d3, 20px -20px 40px #ededed",
});

export const titleCls = style({
  color: "#636661",
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
  justifyContent: "center",
  marginTop: "1rem",
});

export const controlsFuncCls = style({
  flex: "auto",
});

export const padsCls = style({
  display: "flex",
});

export const padsWrapperCls = style({
  marginTop: "1rem",
  display: "inline-block",
});

export const settingsTrackDisplayCls = style({
  color: "#636661",
  borderRadius: "20px",
  background: "#e0e0e0",
  boxShadow: "inset -5px 5px 10px #bebebe, inset 5px -5px 10px #fff",
  height: "110px",
});

export const settingsTrackDisplayDlCls = style({
  display: "inline-flex",
});

globalStyle(`${settingsTrackDisplayDlCls} > dt`, {
  marginLeft: "1rem",
  width: "20px",
});

export const settingsTrackDisplayDlDdCls = style({
  margin: "0",
});

globalStyle(`${settingsTrackDisplayDlDdCls} > div:first-of-type`, {
  display: "inline",
  background: "#fff",
  fontWeight: "bold",
  padding: "0 8px",
  borderRadius: "8px",
});

globalStyle(`${settingsTrackDisplayDlDdCls} > div:nth-of-type(2)`, {
  fontSize: " 0.8rem",
});

export const settingsBpmCls = style({
  display: "grid",
  margin: "0 0.8rem 0 2rem",
});

export const settingsTrackButtonAreaCls = style({
  marginTop: "0.5rem",
});

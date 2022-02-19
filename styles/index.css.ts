import { style, globalStyle } from "@vanilla-extract/css";

export const containerCls = style({
  minHeight: "100vh",
  backgroundColor: "var(--color-grayLight)",
});

export const heroImage = style({
  width: "100%",
  position: "relative",
  padding: "10% 0",
  backgroundColor: "var(--color-grayLight)",
});

export const heroText = style({
  textAlign: "center",
  color: "var(--color-black)",
  backgroundColor: "var(--color-grayLight)",
});

globalStyle(`${heroText} > h1`, {
  fontWeight: 300,
  margin: "20px",
  "@media": {
    "screen and (max-width: 480px)": {
      fontSize: "1rem",
      margin: "4.5rem auto",
    },
  },
});

export const seqArea = style({
  position: "relative",
  width: "100%",
  height: 150,
});

export const gearsSection = style({
  display: "flex",
  flexWrap: "wrap",
  justifyContent: "center",
  marginBottom: "120px",
  color: "var(--color-black)",
  fontWeight: 300,
  "@media": {
    "screen and (max-width: 480px)": {
      fontSize: ".8rem",
    },
  },
});

export const gearsSectionDetail = style({
  margin: "0",
});

globalStyle(`${gearsSectionDetail} > dl > dd`, {
  margin: "0 20px",
});

export const gearsSectionDetailList = style({
  padding: "20px 0",
  margin: "0",
});

globalStyle(`${gearsSectionDetailList} > li`, {
  margin: "0",
});

export const btnArea = style({
  textAlign: "center",
  marginBottom: "20px",
});

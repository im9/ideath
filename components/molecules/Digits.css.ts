import { style, globalStyle } from "@vanilla-extract/css";

const digitsColor = "var(--color-white)";

const dWidth = 14;
const dHight = 6;
const dHightHalf = dHight / 2;
const extraHight = -dHight;
const gap = dHight / 2;
const firstTop = gap + dHight;
const secondTop = firstTop + dWidth + dHight + firstTop;
const thirdTop = firstTop + dWidth * 2 + dHight + firstTop + gap;
const middleTop = firstTop + dWidth + firstTop / 2;
const dLeft = -(gap + dHight);
const dRight = gap + dWidth;

export const displayCls = style({
  display: "inline-block",
  padding: "6px 6px 12px 18px",
  borderRadius: "20px",
  background: "var(--color-grayLight)",
  boxShadow: `inset -5px 5px 10px var(--color-gray2), inset 5px -5px 10px ${digitsColor}`,
  margin: "0.5rem 0",
});

export const displayLabelCls = style({
  display: "inline-block",
  fontSize: "0.5rem",
  color: "var(--color-gray)",
  marginBottom: "0.2rem",
  background: "var(--color-white)",
  padding: "0 8px",
  borderRadius: "8px",
});

export const digitsCls = style({});

globalStyle(`${digitsCls} > div`, {
  position: "relative",
  width: "30px",
  height: "60px",
  display: "inline-block",
  margin: "0 0.7em",
});

export const parentDClass = style({
  position: "absolute",
  display: "block",
  background: digitsColor,
  ":before": {
    content: " ",
    position: "absolute",
    display: "block",
    width: 0,
    height: 0,
    borderStyle: "solid",
  },
  ":after": {
    content: " ",
    position: "absolute",
    display: "block",
    width: 0,
    height: 0,
    borderStyle: "solid",
  },
});

export const d1Cls = style([
  parentDClass,
  {
    width: dWidth,
    height: dHight,
    ":before": {
      left: extraHight,
      borderWidth: `0 ${dHight}px ${dHight}px 0`,
      borderColor: `transparent ${digitsColor} transparent transparent`,
    },
    ":after": {
      right: extraHight,
      borderWidth: `${dHight}px ${dHight}px 0 0`,
      borderColor: `${digitsColor} transparent transparent transparent`,
    },
  },
]);

export const d2Cls = style([
  parentDClass,
  {
    left: dLeft,
    top: firstTop,
    width: dHight,
    height: dWidth,
    ":before": {
      top: extraHight,
      borderWidth: `${dHight}px 0 0 ${dHight}px`,
      borderColor: `transparent transparent transparent ${digitsColor}`,
    },
    ":after": {
      bottom: extraHight,
      borderWidth: `${dHight}px ${dHight}px 0 0`,
      borderColor: `${digitsColor} transparent transparent transparent`,
    },
  },
]);

export const d3Cls = style([
  parentDClass,
  {
    left: dRight,
    top: firstTop,
    width: dHight,
    height: dWidth,
    ":before": {
      top: extraHight,
      borderWidth: `0 0 ${dHight}px ${dHight}px`,
      borderColor: `transparent transparent ${digitsColor} transparent`,
    },
    ":after": {
      bottom: extraHight,
      borderWidth: `0 ${dHight}px ${dHight}px 0`,
      borderColor: `transparent ${digitsColor} transparent transparent`,
    },
  },
]);

export const d4Cls = style([
  parentDClass,
  {
    left: dLeft,
    top: secondTop,
    width: dHight,
    height: dWidth,
    ":before": {
      top: extraHight,
      borderWidth: `${dHight}px 0 0 ${dHight}px`,
      borderColor: `transparent transparent transparent ${digitsColor}`,
    },
    ":after": {
      bottom: extraHight,
      borderWidth: `${dHight}px ${dHight}px 0 0`,
      borderColor: `${digitsColor} transparent transparent transparent`,
    },
  },
]);

export const d5Cls = style([
  parentDClass,
  {
    left: dRight,
    top: secondTop,
    width: dHight,
    height: dWidth,
    ":before": {
      top: extraHight,
      borderWidth: `0 0 ${dHight}px ${dHight}px`,
      borderColor: `transparent transparent ${digitsColor} transparent`,
    },
    ":after": {
      bottom: extraHight,
      borderWidth: `0 ${dHight}px ${dHight}px 0`,
      borderColor: `transparent ${digitsColor} transparent transparent`,
    },
  },
]);

export const d6Cls = style([
  parentDClass,
  {
    top: middleTop,
    width: dWidth,
    height: dHight,
    ":before": {
      left: extraHight,
      borderWidth: `${dHightHalf}px ${dHight}px ${dHightHalf}px 0`,
      borderColor: `transparent ${digitsColor} transparent transparent`,
    },
    ":after": {
      right: extraHight,
      borderWidth: `${dHightHalf}px 0 ${dHightHalf}px ${dHight}px`,
      borderColor: `transparent transparent transparent ${digitsColor}`,
    },
  },
]);

export const d7Cls = style([
  parentDClass,
  {
    top: thirdTop,
    width: dWidth,
    height: dHight,
    ":before": {
      left: extraHight,
      borderWidth: `0 0 ${dHight}px ${dHight}px`,
      borderColor: `transparent transparent ${digitsColor} transparent`,
    },
    ":after": {
      right: extraHight,
      borderWidth: `${dHight}px 0 0 ${dHight}px`,
      borderColor: `transparent transparent transparent ${digitsColor}`,
    },
  },
]);

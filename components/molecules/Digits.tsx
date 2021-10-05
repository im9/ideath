import React from "react";
import {
  displayCls,
  digitsCls,
  d1Cls,
  d2Cls,
  d3Cls,
  d4Cls,
  d5Cls,
  d6Cls,
  d7Cls,
} from "./Digits.css";

type Props = {
  bpm?: String | Number;
  min?: Number;
  max?: Number;
};

const Digits: React.FC<Props> = ({ bpm = 0, min = 20, max = 300 }) => {
  if (bpm > max || bpm < min) return <></>;

  const digits = String(bpm)
    .padStart(String(max).length, "0")
    .split("")
    .map((value, index) => {
      switch (value) {
        case "1":
          return (
            <div key={index}>
              <span className={d3Cls}></span>
              <span className={d5Cls}></span>
            </div>
          );
        case "2":
          return (
            <div key={index}>
              <span className={d1Cls}></span>
              <span className={d3Cls}></span>
              <span className={d4Cls}></span>
              <span className={d6Cls}></span>
              <span className={d7Cls}></span>
            </div>
          );
        case "3":
          return (
            <div key={index}>
              <span className={d1Cls}></span>
              <span className={d3Cls}></span>
              <span className={d5Cls}></span>
              <span className={d6Cls}></span>
              <span className={d7Cls}></span>
            </div>
          );
        case "4":
          return (
            <div key={index}>
              <span className={d2Cls}></span>
              <span className={d3Cls}></span>
              <span className={d5Cls}></span>
              <span className={d6Cls}></span>
            </div>
          );
        case "5":
          return (
            <div key={index}>
              <span className={d1Cls}></span>
              <span className={d2Cls}></span>
              <span className={d5Cls}></span>
              <span className={d6Cls}></span>
              <span className={d7Cls}></span>
            </div>
          );
        case "6":
          return (
            <div key={index}>
              <span className={d2Cls}></span>
              <span className={d4Cls}></span>
              <span className={d5Cls}></span>
              <span className={d6Cls}></span>
              <span className={d7Cls}></span>
            </div>
          );
        case "7":
          return (
            <div key={index}>
              <span className={d1Cls}></span>
              <span className={d3Cls}></span>
              <span className={d5Cls}></span>
            </div>
          );
        case "8":
          return (
            <div key={index}>
              <span className={d1Cls}></span>
              <span className={d2Cls}></span>
              <span className={d3Cls}></span>
              <span className={d4Cls}></span>
              <span className={d5Cls}></span>
              <span className={d6Cls}></span>
              <span className={d7Cls}></span>
            </div>
          );
        case "9":
          return (
            <div key={index}>
              <span className={d1Cls}></span>
              <span className={d2Cls}></span>
              <span className={d3Cls}></span>
              <span className={d5Cls}></span>
              <span className={d6Cls}></span>
            </div>
          );
        default:
          // 0
          return (
            <div key={index}>
              <span className={d1Cls}></span>
              <span className={d2Cls}></span>
              <span className={d3Cls}></span>
              <span className={d4Cls}></span>
              <span className={d5Cls}></span>
              <span className={d7Cls}></span>
            </div>
          );
      }
    });

  return (
    <div className={displayCls}>
      <div className={digitsCls}>{digits}</div>
    </div>
  );
};

export default Digits;

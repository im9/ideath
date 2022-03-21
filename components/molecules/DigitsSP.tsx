import React from "react";
import * as styles from "./DigitsSP.css";

type Props = {
  label?: String;
  bpm?: String | Number;
  min?: Number;
  max?: Number;
};

/**
 * FIXME: Digits と統合
 */
const Digits: React.FC<Props> = ({
  label = "",
  bpm = 0,
  min = 20,
  max = 300,
}) => {
  if (bpm > max || bpm < min) return <></>;

  const digits = String(bpm)
    .padStart(String(max).length, "0")
    .split("")
    .map((value, index) => {
      switch (value) {
        case "1":
          return (
            <div key={index}>
              <span className={styles.d3Cls}></span>
              <span className={styles.d5Cls}></span>
            </div>
          );
        case "2":
          return (
            <div key={index}>
              <span className={styles.d1Cls}></span>
              <span className={styles.d3Cls}></span>
              <span className={styles.d4Cls}></span>
              <span className={styles.d6Cls}></span>
              <span className={styles.d7Cls}></span>
            </div>
          );
        case "3":
          return (
            <div key={index}>
              <span className={styles.d1Cls}></span>
              <span className={styles.d3Cls}></span>
              <span className={styles.d5Cls}></span>
              <span className={styles.d6Cls}></span>
              <span className={styles.d7Cls}></span>
            </div>
          );
        case "4":
          return (
            <div key={index}>
              <span className={styles.d2Cls}></span>
              <span className={styles.d3Cls}></span>
              <span className={styles.d5Cls}></span>
              <span className={styles.d6Cls}></span>
            </div>
          );
        case "5":
          return (
            <div key={index}>
              <span className={styles.d1Cls}></span>
              <span className={styles.d2Cls}></span>
              <span className={styles.d5Cls}></span>
              <span className={styles.d6Cls}></span>
              <span className={styles.d7Cls}></span>
            </div>
          );
        case "6":
          return (
            <div key={index}>
              <span className={styles.d2Cls}></span>
              <span className={styles.d4Cls}></span>
              <span className={styles.d5Cls}></span>
              <span className={styles.d6Cls}></span>
              <span className={styles.d7Cls}></span>
            </div>
          );
        case "7":
          return (
            <div key={index}>
              <span className={styles.d1Cls}></span>
              <span className={styles.d3Cls}></span>
              <span className={styles.d5Cls}></span>
            </div>
          );
        case "8":
          return (
            <div key={index}>
              <span className={styles.d1Cls}></span>
              <span className={styles.d2Cls}></span>
              <span className={styles.d3Cls}></span>
              <span className={styles.d4Cls}></span>
              <span className={styles.d5Cls}></span>
              <span className={styles.d6Cls}></span>
              <span className={styles.d7Cls}></span>
            </div>
          );
        case "9":
          return (
            <div key={index}>
              <span className={styles.d1Cls}></span>
              <span className={styles.d2Cls}></span>
              <span className={styles.d3Cls}></span>
              <span className={styles.d5Cls}></span>
              <span className={styles.d6Cls}></span>
            </div>
          );
        default:
          // 0
          return (
            <div key={index}>
              <span className={styles.d1Cls}></span>
              <span className={styles.d2Cls}></span>
              <span className={styles.d3Cls}></span>
              <span className={styles.d4Cls}></span>
              <span className={styles.d5Cls}></span>
              <span className={styles.d7Cls}></span>
            </div>
          );
      }
    });

  return (
    <div className={styles.displayCls}>
      <div className={styles.displayLabelCls}>{label}</div>
      <div className={styles.digitsCls}>{digits}</div>
    </div>
  );
};

export default Digits;

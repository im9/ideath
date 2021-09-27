import React from "react";
import styles from "./Digits.module.scss";

type Props = {
  bpm?: String | Number;
  on?: boolean;
};

const Digits: React.FC<Props> = ({ bpm = 0 }) => {
  if (bpm > 300 || bpm < 20) return <></>;

  const digits = String(bpm)
    .padStart(3, "0")
    .split("")
    .map((value, index) => {
      let style;
      switch (value) {
        case "1":
          style = styles.one;
          break;
        case "2":
          style = styles.two;
          break;
        case "3":
          style = styles.three;
          break;
        case "4":
          style = styles.four;
          break;
        case "5":
          style = styles.five;
          break;
        case "6":
          style = styles.six;
          break;
        case "7":
          style = styles.seven;
          break;
        case "8":
          style = styles.eight;
          break;
        case "9":
          style = styles.nine;
          break;
        default:
          style = styles.zero;
      }
      return (
        <div key={index} className={style}>
          <span className={styles.d1}></span>
          <span className={styles.d2}></span>
          <span className={styles.d3}></span>
          <span className={styles.d4}></span>
          <span className={styles.d5}></span>
          <span className={styles.d6}></span>
          <span className={styles.d7}></span>
        </div>
      );
    });

  return (
    <div className={styles.display}>
      <div className={styles.digits}>{digits}</div>
    </div>
  );
};

export default Digits;

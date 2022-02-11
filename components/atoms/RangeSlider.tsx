import React from "react";
import * as styles from "./RangeSlider.css";

type Props = {};

const RangeSlider: React.FC<Props> = () => {
  return (
    <div className={styles.slider}>
      <div className={styles.sliderBox}>
        <span className={styles.sliderBtn}></span>
        <span className={styles.sliderColor}></span>
        <span className={styles.sliderTooltip}>50%</span>
      </div>
    </div>
  );
};

export default RangeSlider;

import React, { useCallback } from "react";
import * as styles from "./Toggle.css";

type Props = {
  isOn: boolean;
  onClick: Function;
};

const Toggle: React.FC<Props> = ({ isOn, onClick }) => {
  const indicatorOnCls = isOn ? styles.indicatorOn : "";

  const handleClick = useCallback((e: any) => {
    onClick(e);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  return (
    <label className={styles.label}>
      <div className={styles.toggle} onClick={handleClick}>
        <input
          className={styles.toggleState}
          type="checkbox"
          name="check"
          value="check"
        />
        <div className={`${styles.indicator} ${indicatorOnCls}`}></div>
      </div>
    </label>
  );
};

export default Toggle;

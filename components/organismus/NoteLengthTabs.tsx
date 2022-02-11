import React from "react";
import Icons from "@/components/atoms/Icons";
import { NOTES_LENGTH } from "@/constants/fm";
import * as styles from "./NoteLengthTabs.css";

type Props = {
  selectedValue: any;
  onClick: Function;
};

const NoteLengthTabs: React.FC<Props> = ({ selectedValue, onClick }) => {
  /**
   * 音符の選択タブを描画する
   */
  const allNotesLength = NOTES_LENGTH.map((key, index) => {
    const checkedCls =
      selectedValue === key ? styles.segmentedControlChecked : "";
    return (
      <div key={`${key}_${index}`}>
        <input defaultChecked={!!checkedCls} />
        <label
          className={`${styles.tab} ${checkedCls}`}
          onClick={() => onClick(key)}
        >
          {/* <p>{key}</p> */}
          <Icons name={`Note${key}`} />
        </label>
      </div>
    );
  });

  return (
    <div>
      <label className={styles.settingLabel}>Note Length</label>
      <div className={styles.segmentedControl}>{allNotesLength}</div>
    </div>
  );
};

export default NoteLengthTabs;

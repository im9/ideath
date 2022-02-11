import React from "react";
import * as styles from "./NoteOctaveTabs.css";

type Props = {
  selectedValue: any;
  onClick: Function;
};

const NoteOctaveTabs: React.FC<Props> = ({ selectedValue, onClick }) => {
  /**
   * 音符のオクターブの選択タブを描画する
   */
  const allNoteOctaves = [...Array(4)].map((_, index) => {
    const currentOctave = index + 1;
    const checkedCls =
      selectedValue === currentOctave ? styles.segmentedControlChecked : "";
    return (
      <div key={`${currentOctave}_${index}`}>
        <input defaultChecked={!!checkedCls} />
        <label
          className={`${styles.tab} ${checkedCls}`}
          onClick={() => onClick(currentOctave)}
        >
          <p>{currentOctave}</p>
        </label>
      </div>
    );
  });

  return (
    <div>
      <label className={styles.settingLabel}>Octave</label>
      <div className={styles.segmentedControl}>{allNoteOctaves}</div>
    </div>
  );
};

export default NoteOctaveTabs;

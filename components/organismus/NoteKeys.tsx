import React from "react";
import { NOTES } from "@/constants/fm";
import * as styles from "./NoteKeys.css";

type Props = {
  octaveIndex: number;
  selectedOctave: number; // TODO: 和音に対応
  selectedValues: string[]; // TODO: 和音に対応
  onClick: Function;
};

const NoteKeys: React.FC<Props> = ({
  octaveIndex,
  selectedOctave,
  selectedValues,
  onClick,
}) => {
  const notes = NOTES[octaveIndex];

  /**
   * 白鍵を描画する
   */
  const allWhiteKeys = notes
    ?.flat()
    ?.map(({ note, octave }: { note: string; octave: number }, index) => {
      if (note?.includes("#")) {
        return null;
      }

      const checkedCls =
        selectedValues.includes(note) && selectedOctave === octave
          ? styles.whiteKeyPushed
          : "";
      return (
        <div
          key={index}
          className={styles.whiteKeyBlock}
          onClick={() => onClick(note, octave)}
        >
          <div className={`${styles.whiteKey} ${checkedCls}`} />
        </div>
      );
    })
    .filter((el) => el);

  /**
   * 黒鍵を描画する
   */
  const allBlackKeys = notes
    ?.flat()
    ?.map(({ note, octave }: { note: string; octave: number }, index) => {
      if (!note?.includes("#")) {
        return null;
      }

      const checkedCls =
        selectedValues.includes(note) && selectedOctave === octave
          ? styles.blackKeyPushed
          : "";

      let blackKeyBlockPosCls = "";
      if ([1, 8, 13, 20].includes(index)) {
        blackKeyBlockPosCls = styles.blackKeyBlockRight;
      } else if ([3, 15].includes(index)) {
        blackKeyBlockPosCls = styles.blackKeyBlockCenter;
      } else if ([6, 10, 18, 22].includes(index)) {
        blackKeyBlockPosCls = styles.blackKeyBlockLeft;
      }

      return (
        <div
          key={index}
          className={`${styles.blackKeyBlock} ${blackKeyBlockPosCls}`}
          onClick={() => onClick(note, octave)}
        >
          <div className={`${styles.blackKey} ${checkedCls}`} />
        </div>
      );
    })
    .filter((el) => el);

  return (
    <div className={styles.notes}>
      <div className={styles.blackKeys1}>{allBlackKeys.splice(0, 3)}</div>
      <div className={styles.blackKeys2}>{allBlackKeys.splice(0, 2)}</div>
      <div className={styles.blackKeys3}>{allBlackKeys.splice(0, 3)}</div>
      <div className={styles.blackKeys4}>{allBlackKeys.splice(0, 2)}</div>
      <div className={styles.whiteKeys1}>{allWhiteKeys.splice(0, 4)}</div>
      <div className={styles.whiteKeys2}>{allWhiteKeys.splice(0, 3)}</div>
      <div className={styles.whiteKeys3}>{allWhiteKeys.splice(0, 4)}</div>
      <div className={styles.whiteKeys4}>{allWhiteKeys.splice(0, 3)}</div>
    </div>
  );
};

export default NoteKeys;

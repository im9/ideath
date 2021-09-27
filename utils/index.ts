/**
 * 行列を初期化する
 * @param col 行
 * @param row 列
 * @returns number[][]
 */
export const getDefaultMatrix = (
  col: number,
  row: number,
  defaultValue = 0
): number[][] => {
  if (!col || !row) return [];
  return [...Array(col)].map(() => Array(row).fill(defaultValue));
};

/**
 * BPM からテンポを算出する
 * @param bpm
 * @returns tempo
 */
export const getTempo = (bpm: number = 120): number => {
  // 一分間 / bpm * ms
  return (60 / bpm) * 250;
};

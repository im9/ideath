import * as Tone from "tone";

/**
 * 行列を初期化する
 * @param col 行
 * @param row 列
 * @returns number[][]
 */
export const getDefaultMatrix = (
  col: number,
  row: number,
  defaultValue: any = 0
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

/**
 * サンプル音のオブジェクトを生成する
 * @param path
 * @param d ディストーションのパラメータ
 * @param r リバーブのパラメータ
 * @returns Tone.Player
 */
export const getTonePlayer = (
  path: string = "",
  d: number | null = 0,
  r: number | null = 0
) => {
  if (!path) return;

  const player = new Tone.Player(path).toDestination();
  const distortion = d ? new Tone.Distortion(d).toDestination() : null;
  const reverb = r ? new Tone.Reverb(r).toDestination() : null;
  if (distortion) player.connect(distortion);
  if (reverb) player.connect(reverb);
  return player;
};

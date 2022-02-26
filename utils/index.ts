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
 * @param v ヴォリュームのパラメータ
 * @returns Tone.Player
 */
export const getTonePlayer = (
  path: string = "",
  d: number | null = 0,
  r: number | null = 0,
  v: number | null = -40
) => {
  if (!path) return;

  const player = new Tone.Player(path).toDestination();
  const distortion = d ? new Tone.Distortion(d).toDestination() : null;
  const reverb = r ? new Tone.Reverb(r).toDestination() : null;
  const volume = v ? new Tone.Volume(v).toDestination() : null;
  if (distortion) player.connect(distortion);
  if (reverb) player.connect(reverb);
  if (volume) player.connect(volume);
  return player;
};

/**
 * 任意の桁で四捨五入する
 * @param {number} value 四捨五入する数値
 * @param {number} base どの桁で四捨五入するか（10 => 10の位、0.1 => 小数第１位）
 * @return {number} 四捨五入した値
 */
export const round = (value: number, base: number) => {
  return Math.round(value * base) / base;
};

/**
 * 任意の桁で切り上げる
 * @param {number} value 切り上げる数値
 * @param {number} base どの桁で切り上げするか（10 => 10の位、0.1 => 小数第１位）
 * @return {number} 切り上げした値
 */
export const ceil = (value: number, base: number) => {
  return Math.ceil(value * base) / base;
};

/**
 * 任意の桁で切り捨てる
 * @param {number} value 切り捨てる数値
 * @param {number} base どの桁で切り捨てするか（10 => 10の位、0.1 => 小数第１位）
 * @return {number} 切り捨てした値
 */
export const floor = (value: number, base: number) => {
  return Math.floor(value * base) / base;
};

/**
 * 百分率表示する
 * @param {number} value 浮動小数点の
 * @return {number} 百分率の文字列
 */
export const percent = (value: number | undefined) => {
  return Math.floor(value ? value * 100 : 0);
};

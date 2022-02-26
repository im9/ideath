import * as Tone from "tone";
import type { NextPage } from "next";
import Head from "next/head";
import { useState, useRef, useCallback, useEffect, useContext } from "react";
import { useRecoilState } from "recoil";
import { Context } from "@/contexts/state";
import MainLayout from "@/components/layouts/MainLayout";
import PlayButton from "@/components/atoms/PlayButton";
import CircleButton from "@/components/atoms/CircleButton";
import SquareButton from "@/components/atoms/SquareButton";
import Knob from "@/components/atoms/Knob";
import NoteLengthTabs from "@/components/organismus/NoteLengthTabs";
import NoteOctaveTabs from "@/components/organismus/NoteOctaveTabs";
import NoteKeys from "@/components/organismus/NoteKeys";
import { useModal } from "hooks/usePanel";
import {
  fmSynthOptionsState,
  fmSynthStepPadsState,
} from "@/store/atoms/fmSynth";
import { getDefaultMatrix, getTempo, percent } from "@/utils";
import { TRACK_LENGTH, STEP_LENGTH } from "@/constants/fm";
import * as styles from "@/styles/fm.css";

import dynamic from "next/dynamic";
// import p5Types from "p5";

const Sketch = dynamic(import("react-p5"), { ssr: false });

export const SketchComponent = ({ spectrumWave }: any) => {
  let width = 960;
  let heigth = 48;

  const preload = () => {};

  const setup = (p5: any, canvasParentRef: Element) => {
    p5.createCanvas(width, heigth).parent(canvasParentRef);

    p5.noStroke();
    p5.frameRate(30);
  };

  const draw = (p5: any) => {
    p5.background(225);
    p5.stroke(255);

    const buffer = spectrumWave?.getValue();
    const len = buffer?.length;
    if (buffer) {
      for (let i = 0; i < len; i++) {
        const x = p5.map(i, 0, len, 0, width);
        const y = p5.map(buffer[i], -1, 1, 0, heigth);
        p5.point(x, y);
      }
    }
  };

  const windowResized = (p5: any) => {
    p5.resizeCanvas(p5.windowWidth / 1.1, heigth);
  };

  return (
    <Sketch
      preload={preload}
      setup={setup}
      draw={draw}
      windowResized={windowResized}
    />
  );
};

/**
 * FM
 */
const FM: NextPage = () => {
  // context
  const {
    state: { master },
    dispatch,
  }: any = useContext(Context);

  // state
  const [fmSynth, setFmSynth] = useState<any>(null);
  const [fmSynthOptions, setFmSynthOptions] =
    useRecoilState(fmSynthOptionsState);
  const [fmSynthStepPads, setFmSynthStepPads] =
    useRecoilState(fmSynthStepPadsState);
  const [distortion, setDistortion] = useState<number>(0);
  const [reverb, setReverb] = useState<number>(0);
  const [delay, setDelay] = useState<number>(0);
  const [isCommit, setIsCommit] = useState<boolean>(false);
  const [step, setStep] = useState(0);
  const [displayStep, setDistplayStep] = useState(0);
  const [isDisplayAnalise, setIsDiplayAnalise] = useState(false);

  // リズムマシン系
  const [matrix, setMatrix] = useState(
    getDefaultMatrix(TRACK_LENGTH, STEP_LENGTH)
  );
  const stepRef = useRef(0);
  const intervalRef: any = useRef(null);
  const matrixRef = useRef(matrix);
  const tempoRef = useRef(getTempo(master.bpm));

  // スペクトラムアナライザ
  const [spectrumWave, setSpectrumWave] = useState<any>(null);

  const [Modal, openModal, closeModal, isOpenModal] = useModal("root", {
    preventScroll: true,
  });

  /**
   * タイマーを初期化する
   */
  const initTimer = useCallback(() => {
    clearInterval(intervalRef.current);

    intervalRef.current = setInterval(() => {
      setStep((step) => (step < STEP_LENGTH - 1 ? ++step : 0));
    }, tempoRef.current);
  }, []);

  /**
   * 再生ボタンのクリックイベントをハンドルする
   */
  const handlePlayBtnClick = useCallback(
    async (isStart) => {
      dispatch({
        type: "SET_PLAY",
        payload: isStart,
      });

      // Audio Context 開始
      await Tone.start();

      if (isStart) {
        // 再生ボタンが押下されたらSTEPを更新する
        intervalRef.current = setInterval(() => {
          setStep((step) => (step < STEP_LENGTH - 1 ? ++step : 0));
        }, tempoRef.current);
      } else {
        clearInterval(intervalRef.current);
        intervalRef.current = null;
      }
    },
    [dispatch]
  );

  /**
   * リセットボタンのクリックイベントをハンドルする
   */
  const handleResetBtnClick = useCallback(() => {
    if (isOpenModal) return;
    openModal();

    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  /**
   * リセットモーダルのOKボタンのクリックをハンドルする
   */
  const handleResetOKBtnClick = useCallback(() => {
    // ステップを初期化
    setStep(0);
    stepRef.current = 0;

    // 全トラックの押下状態を初期化
    setMatrix(getDefaultMatrix(TRACK_LENGTH, STEP_LENGTH));
    matrixRef.current = matrix;

    // タイマーを初期化
    dispatch({
      type: "SET_PLAY",
      payload: false,
    });
    clearInterval(intervalRef.current);

    // モーダルを閉じる
    closeModal();

    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  /**
   * アナライザーボタンのクリックをトグルする
   */
  const handleAnaliseToggleBtn = useCallback(() => {
    setIsDiplayAnalise((isDisplayAnalise) => !isDisplayAnalise);
  }, []);

  /**
   * キーボードのクリックイベントをハンドルする
   */
  const HandleNoteKeyClick = async (note: string, octave: number) => {
    if (!Tone.loaded) {
      await Tone.start();
    }
    if (!fmSynth) {
      setFmSynth(new Tone.FMSynth(fmSynthOptions).toDestination());
    }

    if (note) {
      matrix[0][displayStep] = Number(!matrix[0][displayStep]);
      setMatrix(JSON.parse(JSON.stringify(matrix)));
      // fmSynth?.triggerAttackRelease(
      //   `${key}${fmSynthStepPads[step].octaveIndex}`,
      //   `${fmSynthStepPads[step].noteLen}`
      // );
      setFmSynthStepPads((fmSynthStepPads) => {
        const updatePads = fmSynthStepPads.map((pad, index) => {
          if (index === displayStep) {
            return {
              ...pad,
              note,
              octave,
            };
          }
          return pad;
        });
        return updatePads;
      });
      // const sampler = new Tone.Sampler({
      //   urls: {
      //     A1: "A1.mp3",
      //     A2: "A2.mp3",
      //   },
      //   baseUrl: "https://tonejs.github.io/audio/casio/",
      //   onload: () => {
      //     sampler.triggerAttackRelease([`${key}1`], 0.5);
      //   },
      // }).toDestination();
    }
  };

  /**
   * 音符の変更ボタンのクリックイベントをハンドルする
   */
  const handleNoteLengthClick = useCallback(
    (key: string) => {
      setFmSynthStepPads((fmSynthStepPads) => {
        const updatePads = fmSynthStepPads.map((pad, index) => {
          if (index === displayStep) {
            return {
              ...pad,
              noteLen: key,
            };
          }
          return pad;
        });
        return updatePads;
      });
    },
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [displayStep]
  );

  /**
   * オクターブ変更ボタンのクリックイベントをハンドルする
   */
  const handleOctaveBtnClick = useCallback(
    (value: number) => {
      setFmSynthStepPads((fmSynthStepPads) => {
        const updatePads = fmSynthStepPads.map((pad, index) => {
          if (index === displayStep) {
            return {
              ...pad,
              octaveIndex: value,
            };
          }
          return pad;
        });
        return updatePads;
      });
    },
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [displayStep]
  );

  /**
   * ディストーションの値を変更する
   */
  const handleDistortionCtl = useCallback((value) => {
    setDistortion(value * 3);
  }, []);

  /**
   * リバーブの値を変更する
   */
  const handleReverbKnobCtl = useCallback((value) => {
    setReverb(value * 3);
  }, []);

  /**
   * ディレイのisDisあplayAnaliseを変更する
   */
  const handleDelayKnobCtl = useCallback((value) => {
    setDelay(value * 3);
  }, []);

  /**
   * 音量の値を変更する
   */
  const handleVolumeCtl = useCallback(
    (value) => {
      const options = {
        ...fmSynthOptions,
        volume: 1 - (1 - value) * 10,
      };
      setFmSynthOptions(options);
    },
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [fmSynthOptions]
  );

  /**
   * 周波数の値を変更する
   */
  const handleFrequencyKnobCtl = useCallback(
    (value) => {
      const options = {
        ...fmSynthOptions,
        frequency: value * 200,
      };
      setFmSynthOptions(options);
    },
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [fmSynthOptions]
  );

  /**
   * 変調の値を変更する
   */
  const handleModulationKnobCtl = useCallback(
    (value) => {
      const options = {
        ...fmSynthOptions,
        modulationIndex: value * 3,
      };
      setFmSynthOptions(options);
    },
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [fmSynthOptions]
  );

  const handleAttackKnobCtl = useCallback(
    (value) => {
      const options = {
        ...fmSynthOptions,
        modulationEnvelope: {
          ...fmSynthOptions.modulationEnvelope,
          attack: value,
        },
      };
      setFmSynthOptions(options);
    },
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [fmSynthOptions]
  );

  const handleDecayKnobCtl = useCallback(
    (value) => {
      const options = {
        ...fmSynthOptions,
        modulationEnvelope: {
          ...fmSynthOptions.modulationEnvelope,
          decay: value,
        },
      };
      setFmSynthOptions(options);
    },
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [fmSynthOptions]
  );

  const handleSustainKnobCtl = useCallback(
    (value) => {
      const options = {
        ...fmSynthOptions,
        envelope: {
          ...fmSynthOptions.modulationEnvelope,
          sustain: value,
        },
      };
      setFmSynthOptions(options);
    },
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [fmSynthOptions]
  );

  const handleReleaseKnobCtl = useCallback(
    (value) => {
      const options = {
        ...fmSynthOptions,
        modulationEnvelope: {
          ...fmSynthOptions.modulationEnvelope,
          release: value,
        },
      };
      setFmSynthOptions(options);
    },
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [fmSynthOptions]
  );

  /**
   * ハーモニーを変更する
   */
  const handleHarmonicityKnobCtl = useCallback(
    (value) => {
      const options = {
        ...fmSynthOptions,
        harmonicity: value,
      };
      setFmSynthOptions(options);
    },
    [fmSynthOptions, setFmSynthOptions]
  );

  /**
   * オプションの値の変更の確定をハンドルする
   */
  const handleCommit = () => {
    setIsCommit(true);
  };

  /**
   * ディスプレイを描画する
   */
  const display = isDisplayAnalise ? (
    <div className={styles.controlsDisplayCls}>
      <dl>
        <dt></dt>
        <dd>
          <div>Analise</div>
          <div className={styles.controlsDisplayParamAreaCls}>
            <SketchComponent spectrumWave={spectrumWave} />
          </div>
        </dd>
      </dl>
    </div>
  ) : (
    <div className={styles.controlsDisplayCls}>
      <dl>
        <dt></dt>
        <dd>
          <div>Step {displayStep + 1}</div>
          <div className={styles.controlsDisplayParamAreaCls}>
            <div>Note: {fmSynthStepPads[displayStep].note || "--"}</div>
            <div>Octave: {fmSynthStepPads[displayStep].octave}</div>
            <div>Volume: {percent(fmSynthOptions.volume)}</div>
            <div>Distortion: {percent(distortion / 3)}</div>
            <div>Reverb: {percent(reverb / 3)}</div>
            <div>Delay: {percent(delay / 3)}</div>
            <div>Frequency: {percent(fmSynthOptions.frequency / 200)}</div>
            <div>Harmonicity: {percent(fmSynthOptions.harmonicity)}</div>
            <div>Modulation: {percent(fmSynthOptions.modulationIndex / 3)}</div>
            <div>
              Attack: {percent(fmSynthOptions.modulationEnvelope.attack)}
            </div>
            <div>Decay: {percent(fmSynthOptions.modulationEnvelope.decay)}</div>
            <div>
              Sustain: {percent(fmSynthOptions.modulationEnvelope.sustain)}
            </div>
            <div>
              Release: {percent(fmSynthOptions.modulationEnvelope.release)}
            </div>
          </div>
        </dd>
      </dl>
    </div>
  );

  /**
   * 各ステップの選択ボタンを描画する
   */
  const stepSelectBtns = [...Array(16)].map((_, index) => {
    const label = String(index + 1);
    const isCurrent = index === step && master.play;
    return (
      <CircleButton
        key={index}
        label={label}
        active={displayStep === index || isCurrent}
        onClick={() => {
          setDistplayStep(index);
        }}
      />
    );
  });

  /**
   * リセット確認モーダルを描画する
   */
  const resetModal = process.browser ? (
    <Modal>
      <div className={styles.modalStyleCls}>
        <h1>RESET PATTERN</h1>
        <p>Are you sure you want to reset pattern?</p>
        <footer>
          <SquareButton label="CANCEL" small onClick={closeModal} />
          <SquareButton label="OK" small onClick={handleResetOKBtnClick} />
        </footer>
      </div>
    </Modal>
  ) : (
    <></>
  );

  /**
   * エフェクトの操作を適用する
   */
  useEffect(() => {
    const toneDistortion = new Tone.Distortion(
      distortion || 0.1
    ).toDestination();
    const toneReverb = new Tone.Reverb(reverb || 0.1).toDestination();
    const toneDelay = new Tone.FeedbackDelay(delay || 0.1).toDestination();

    fmSynth?.connect(toneDistortion);
    fmSynth?.connect(toneReverb);
    fmSynth?.connect(toneDelay);
  }, [fmSynth, distortion, reverb, delay]);

  /**
   * オプションの適用を確定する
   */
  useEffect(() => {
    if (isCommit) {
      setFmSynth(new Tone.FMSynth(fmSynthOptions).toDestination());
      setIsCommit(false);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [isCommit]);

  /**
   * 参照用の行列の状態を設定する
   */
  useEffect(() => {
    matrixRef.current = matrix;
  }, [matrix]);

  /**
   * 各トラックで押下されているステップの音を再生する
   */
  useEffect(() => {
    stepRef.current = step;
    matrixRef.current.map((col: number[]) => {
      const options = fmSynthStepPads[step];
      if (options?.note) {
        Tone.loaded().then(async () => {
          if (!Tone.loaded) {
            await Tone.start();
          }
          if (!fmSynth) {
            setFmSynth(
              new Tone.FMSynth({
                ...fmSynthOptions,
                volume: options.volume,
              }).toDestination()
            );
          }

          fmSynth?.triggerAttackRelease(
            `${options.note}${options.octave}`,
            options.noteLen
          );

          const wave = new Tone.Waveform();
          Tone.Master.connect(wave);
          setSpectrumWave(wave);
        });
      }
    });
  }, [master.play, step, fmSynth, fmSynthOptions, fmSynthStepPads]);

  return (
    <>
      <div className={styles.containerCls} id="root">
        <Head>
          <title>iDeath</title>
          <meta name="description" content="Generated by create next app" />
          <meta
            name="viewport"
            content="initial-scale=1.0, width=device-width"
          />
          <link rel="icon" href="/favicon.ico" />
        </Head>
        <MainLayout className={styles.mainFrameCls}>
          <h1 className={styles.titleCls}>FM</h1>
          <SquareButton
            label="Analise"
            small
            pushed={isDisplayAnalise}
            onClick={handleAnaliseToggleBtn}
          />
          <div>{display}</div>
          <div>
            <div className={styles.stepBtnAreaLabel}>Step</div>
            <div>{stepSelectBtns}</div>
          </div>
          <div className={styles.controlsCls}>
            <PlayButton pushed={master.play} onClick={handlePlayBtnClick} />
            <div>
              <SquareButton label="RESET" onClick={handleResetBtnClick} />
            </div>
          </div>
          <div className={styles.controlsCls}>
            <Knob
              label="Volume"
              value={fmSynthOptions.volume}
              onUpdate={handleVolumeCtl}
              onCommit={handleCommit}
            />
            <Knob
              label="Distortion"
              value={distortion}
              onUpdate={handleDistortionCtl}
              onCommit={handleCommit}
            />
            <Knob
              label="Reverb"
              value={reverb}
              onUpdate={handleReverbKnobCtl}
              onCommit={handleCommit}
            />
            <Knob
              label="Delay"
              value={delay}
              onUpdate={handleDelayKnobCtl}
              onCommit={handleCommit}
            />
            <Knob
              label="Frequency"
              value={fmSynthOptions.frequency / 200}
              onUpdate={handleFrequencyKnobCtl}
              onCommit={handleCommit}
            />
            <Knob
              label="Harmonicity"
              value={fmSynthOptions.harmonicity}
              onUpdate={handleHarmonicityKnobCtl}
              onCommit={handleCommit}
            />
            <Knob
              label="Modulation"
              value={fmSynthOptions.modulationIndex / 3}
              onUpdate={handleModulationKnobCtl}
              onCommit={handleCommit}
            />
            <Knob
              label="Attack"
              value={fmSynthOptions.modulationEnvelope.attack}
              onUpdate={handleAttackKnobCtl}
              onCommit={handleCommit}
            />
            <Knob
              label="Decay"
              value={fmSynthOptions.modulationEnvelope.decay}
              onUpdate={handleDecayKnobCtl}
              onCommit={handleCommit}
            />
            <Knob
              label="Sustain"
              value={fmSynthOptions.modulationEnvelope.sustain}
              onUpdate={handleSustainKnobCtl}
              onCommit={handleCommit}
            />
            <Knob
              label="Release"
              value={fmSynthOptions.modulationEnvelope.release}
              onUpdate={handleReleaseKnobCtl}
              onCommit={handleCommit}
            />
          </div>
          <div className={styles.notes}>
            <div className={styles.notesOptionArea}>
              <NoteOctaveTabs
                selectedValue={fmSynthStepPads[displayStep].octaveIndex}
                onClick={handleOctaveBtnClick}
              />
              <NoteLengthTabs
                selectedValue={fmSynthStepPads[displayStep].noteLen}
                onClick={handleNoteLengthClick}
              />
            </div>
            <NoteKeys
              octaveIndex={fmSynthStepPads[displayStep].octaveIndex - 1}
              selectedOctave={fmSynthStepPads[displayStep].octave}
              selectedValues={[fmSynthStepPads[displayStep].note]}
              onClick={HandleNoteKeyClick}
            />
          </div>
          {/* {pads} */}
        </MainLayout>
      </div>
      {resetModal}
    </>
  );
};

export default FM;

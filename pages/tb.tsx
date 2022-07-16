import type { NextPage } from "next";
import Head from "next/head";
import React, { useState, useRef, useCallback, useEffect } from "react";
import MainLayout from "@/components/layouts/MainLayout";
import NoteKeys from "@/components/organismus/NoteKeys";
import Digits from "@/components/molecules/Digits";
import DigitsSP from "@/components/molecules/DigitsSP";
import PlayButton from "@/components/atoms/PlayButton";
import SquareButton from "@/components/atoms/SquareButton";
import CircleButton from "@/components/atoms/CircleButton";
import StepPad from "@/components/atoms/StepPad";
import Label from "@/components/atoms/Label";
import Toggle from "@/components/atoms/Toggle";
import Knob from "@/components/atoms/Knob";
import Icons from "@/components/atoms/Icons";
import { useMQ } from "@/hooks/useMQ";
import { DEFAULT_BPM, STEP_LENGTH } from "@/constants/seq";
import { getTempo, getFreq, getDefaultMatrix } from "@/utils";
import * as styles from "@/styles/tb.css";

const TRACK = 1;

/**
 * TB
 */
const Tb: NextPage = () => {
  const [bpm, setBpm] = useState(DEFAULT_BPM);
  const [step, setStep] = useState(0);
  const [matrix, setMatrix] = useState(getDefaultMatrix(TRACK, STEP_LENGTH));
  const [isPlay, setIsPlay] = useState(false);
  const [isTriangleOsc, setIsTriangleOsc] = useState(false);
  const [kick, setKick] = useState();

  const { isMobile } = useMQ();

  const audioContext = useRef<AudioContext | null>(null);
  const processor = useRef<AudioWorkletNode | null>(null);
  const intervalRef = useRef<NodeJS.Timer | null>(null);
  const tempoRef = useRef(getTempo(bpm));

  useEffect(() => {
    audioContext.current = new AudioContext();

    audioContext.current.audioWorklet
      .addModule("js/processor.js?t=" + new Date().getTime())
      .then(() => {
        if (!audioContext.current) return;

        const p = new AudioWorkletNode(audioContext.current, "my-processor");
        p.connect(audioContext.current.destination);

        fetch("wasm/wasm_audioworklet_synth.wasm?t=" + new Date().getTime())
          .then((r) => r.arrayBuffer())
          .then((r) => {
            p.port.postMessage({ type: "loadWasm", data: r });

            processor.current = p;
          });
      });
  }, []);

  useEffect(() => {
    if (isPlay) {
      intervalRef.current = setInterval(() => {
        // bass
        if (Math.random() > 0.7) {
          return;
        }
        const a = [0, 3, 7, 11];
        const c = a[Math.floor(Math.random() * a.length)];
        const note = c + 36;
        const freq = processor?.current?.parameters.get("freq");
        if (freq?.value) {
          freq.value = 440.0 * Math.pow(2.0, (note - 69) / 12);
          processor?.current?.port.postMessage({ type: "trigger" });
        }

        setStep((step) => (step < STEP_LENGTH - 1 ? ++step : 0));
      }, getTempo(tempoRef.current));
    } else {
      intervalRef.current && clearInterval(intervalRef.current);
      intervalRef.current = null;
    }
    return () => {
      intervalRef.current && clearInterval(intervalRef.current);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [isPlay]);

  // useEffect(() => {
  //   tempoRef.current = getTempo(bpm);
  // }, [bpm]);

  const handleKeyClick = useCallback(async (note: string, octave: number) => {
    if (!audioContext.current) return;
    await audioContext.current.resume();

    const freq = processor?.current?.parameters.get("freq");
    if (freq?.value) {
      freq.value = getFreq(note, octave);
      processor?.current?.port.postMessage({ type: "trigger" });
    }
    const test = processor?.current?.parameters.get("steps");
    if (test?.value) {
      test.value = test?.value + 1;
    }
    console.log(test);
  }, []);

  const handlePlayButtonClick = useCallback(async () => {
    if (!audioContext.current) return;
    await audioContext.current.resume();

    setIsPlay((s) => !s);
  }, []);

  const handlePadClick = useCallback(
    (index: any, key: any) => {
      matrix[index][key] = Number(!matrix[index][key]);
      setMatrix(JSON.parse(JSON.stringify(matrix)));
    },
    [matrix]
  );

  // const initTimer = useCallback(() => {
  //   intervalRef.current && clearInterval(intervalRef.current);
  // }, []);

  const BPMDisplay = isMobile ? (
    <DigitsSP label={"BPM"} bpm={bpm} />
  ) : (
    <Digits label={"BPM"} bpm={bpm} />
  );

  const steps = (
    <div className={styles.padsWrapperCls}>
      {[...Array(16)].map((_, col) => {
        const row = TRACK - 1;
        const isPushed = !!matrix[row][col];
        const isCurrent = col === step && isPlay;
        return (
          <StepPad
            key={`step${col}`}
            pushed={isPushed}
            current={isCurrent}
            row={0}
            col={0}
            onClick={() => {
              handlePadClick(row, col);
            }}
          />
        );
      })}
    </div>
  );

  return (
    <div className={styles.containerCls}>
      <Head>
        <title>iDeath</title>
        <meta name="description" content="Generated by create next app" />
        <meta name="viewport" content="initial-scale=1.0, width=device-width" />
        <link rel="icon" href="/favicon.ico" />
      </Head>
      <MainLayout className={styles.mainFrameCls}>
        <h1 className={styles.titleCls}>TBR</h1>
        <div className={styles.settingsCls}>
          <div className={styles.oscToggleCls}>
            <div>
              <Label label="Waveform" />
            </div>
            <div>
              <Toggle
                isOn={isTriangleOsc}
                onClick={(e: Event) => {
                  e.preventDefault();

                  const mode = processor?.current?.parameters.get("mode");
                  if (mode?.value !== undefined) {
                    mode.value = mode.value === 0 ? 1 : 0;
                    setIsTriangleOsc(!!mode.value);
                  }
                }}
              />
              <Icons name="TriangleWave" />
            </div>
          </div>
          <div className={styles.knobsCls}>
            <Knob
              label="CutOff"
              value={1}
              onUpdate={(value: number) => {
                const cutoff = processor?.current?.parameters.get("cutoff");
                if (cutoff?.value) {
                  cutoff.value = value * 100 || 1;
                }
              }}
            />
            <Knob
              label="Resonance"
              value={1}
              onUpdate={(value: number) => {
                const resonance = processor?.current?.parameters.get("q");
                if (resonance?.value) {
                  resonance.value = value * 10 || 1;
                }
              }}
            />
            <Knob
              label="Mod"
              value={1}
              onUpdate={(value: number) => {
                const amount = processor?.current?.parameters.get("amount");
                if (amount?.value) {
                  amount.value = value || 1;
                }
              }}
            />
            <Knob
              label="Decay"
              value={1}
              onUpdate={(value: number) => {
                const decay = processor?.current?.parameters.get("decay");
                if (decay?.value) {
                  decay.value = value || 0.1;
                }
              }}
            />
            <Knob
              label="Amp"
              value={1}
              onUpdate={(value: number) => {
                const amp = processor?.current?.parameters.get("amp");
                if (amp?.value) {
                  amp.value = value || 0.1;
                }
              }}
            />
          </div>
          <div className={styles.settingsBpmCls}>
            {
              <>
                <CircleButton
                  label="＋"
                  small={isMobile}
                  onClick={() => {
                    setBpm((s) => {
                      return s >= 300 ? s : (s += 1);
                    });
                    // initTimer();
                  }}
                />
                <CircleButton
                  label="−"
                  small={isMobile}
                  onClick={() => {
                    setBpm((s) => {
                      return s <= 20 ? s : (s -= 1);
                    });
                    // initTimer();
                  }}
                />
              </>
            }
          </div>
          <div>{BPMDisplay}</div>
        </div>

        <div className={styles.controlsCls}>
          {steps}
          {/* <div>
            <SquareButton label="RESET" onClick={() => {}} />
          </div> */}

          <div>
            <div className={styles.masterControlCls}>
              <PlayButton pushed={isPlay} onClick={handlePlayButtonClick} />
            </div>
            <NoteKeys
              octaveIndex={1}
              selectedOctave={1}
              selectedValues={[]}
              onClick={(note: string, octave: number) => {
                handleKeyClick(note, octave);
              }}
            />
          </div>
        </div>
      </MainLayout>
    </div>
  );
};

export default Tb;

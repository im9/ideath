import React, { useState, useEffect, useCallback } from "react";
import * as Tone from "tone";
import Pad from "@/components/atoms/Pad";
import Kick from "@/utils/kick";
import Snare from "@/utils/snare";
import Hihat from "@/utils/hihat";
import Clap from "@/utils/clap";
import Audio from "@/utils/audio";
import styles from "./Pads.module.scss";

type Props = {};

const Music: React.FC<Props> = () => {
  const [audio, setAudio]: any = useState({});

  useEffect(() => {
    setAudio(new Audio());
  }, [setAudio]);

  // const handleToneClick = useCallback(() => {
  //   const synth = new Tone.Synth().toDestination();
  //   // synth.triggerAttackRelease("C4", "8n");
  //   const now = Tone.now();
  //   const filter = new Tone.Filter(400, "bandpass").toDestination();
  //   const feedbackDelay = new Tone.FeedbackDelay(0.125, 0.6).toDestination();
  //   synth.connect(filter);
  //   synth.connect(feedbackDelay);
  //   // trigger the attack immediately
  //   synth.triggerAttack("C4", now);
  //   // wait one second before triggering the release
  //   synth.triggerRelease(now + 1);
  // }, []);

  // 1
  const handleS1Click = useCallback(() => {
    const player = new Tone.Player("/samples/808/CB.WAV").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    player.connect(distortion);
    Tone.loaded().then(() => {
      player.start();
    });
  }, []);

  // 2
  const handleS2Click = useCallback(() => {
    const player = new Tone.Player("/samples/voice/ah.wav").toDestination();
    Tone.loaded().then(() => {
      player.start();
    });
  }, []);

  // 3
  const handleS3Click = useCallback(() => {
    const player = new Tone.Player("/samples/808/CB.WAV").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    player.connect(distortion);
    Tone.loaded().then(() => {
      player.start();
    });
  }, []);

  // 4
  const handleS4Click = useCallback(() => {
    const player = new Tone.Player("/samples/808/CB.WAV").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    player.connect(distortion);
    Tone.loaded().then(() => {
      player.start();
    });
  }, []);

  // const handleCLClick = useCallback(() => {
  //   const player = new Tone.Player(
  //     "/samples/Andy Warhol interview 1966.wav"
  //   ).toDestination();
  //   const distortion = new Tone.Distortion(0.4).toDestination();
  //   const crusher = new Tone.BitCrusher(4).toDestination();
  //   player.connect(distortion);
  //   const filter = new Tone.Filter(400, "lowpass").toDestination();
  //   player.connect(filter);
  //   Tone.loaded().then(() => {
  //     player.start();
  //   });
  // }, []);

  // 5
  const handleHHClick = useCallback(() => {
    let hihat = new Hihat(audio.ctx);
    hihat.trigger(audio.ctx.currentTime);
  }, [audio.ctx]);

  // 6
  const handleHH2Click = useCallback(() => {
    const player = new Tone.Player("/samples/808/CP.WAV").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    const crusher = new Tone.BitCrusher(4).toDestination();
    player.connect(distortion);
    const filter = new Tone.Filter(400, "lowpass").toDestination();
    player.connect(filter);
    Tone.loaded().then(() => {
      player.start();
    });
  }, []);

  // 7
  const handleHH3Click = useCallback(() => {
    const player = new Tone.Player(
      "/samples/sd/rytm-00-hard.wav"
    ).toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    const crusher = new Tone.BitCrusher(4).toDestination();
    player.connect(distortion);
    const filter = new Tone.Filter(400, "lowpass").toDestination();
    player.connect(filter);
    Tone.loaded().then(() => {
      player.start();
    });
  }, []);

  // 8
  const handleHH4Click = useCallback(() => {
    const player = new Tone.Player("/samples/techno/001_1.wav").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    const crusher = new Tone.BitCrusher(4).toDestination();
    player.connect(distortion);
    const filter = new Tone.Filter(400, "lowpass").toDestination();
    player.connect(filter);
    Tone.loaded().then(() => {
      player.start();
    });
  }, []);

  // 9
  const handleSnareClick = useCallback(() => {
    let snare = new Snare(audio.ctx);
    snare.trigger(audio.ctx.currentTime);
  }, [audio.ctx]);

  // 10
  const handleSnare2Click = useCallback(() => {
    let clap = new Clap(audio.ctx);
    clap.trigger(audio.ctx.currentTime);
  }, [audio.ctx]);

  // 11
  const handleSnare3Click = useCallback(() => {
    const player = new Tone.Player("/samples/808/CP.WAV").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    const crusher = new Tone.BitCrusher(4).toDestination();
    player.connect(distortion);
    const filter = new Tone.Filter(400, "lowpass").toDestination();
    player.connect(filter);
    Tone.loaded().then(() => {
      player.start();
    });
  }, []);

  // 12
  const handleSnare4Click = useCallback(() => {
    let snare = new Snare(audio.ctx);
    snare.trigger(audio.ctx.currentTime);
  }, [audio.ctx]);

  // 13
  const handleKickClick = useCallback(() => {
    const player = new Tone.Player("/samples/808/RS.WAV").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    const crusher = new Tone.BitCrusher(4).toDestination();
    player.connect(distortion);
    const filter = new Tone.Filter(400, "lowpass").toDestination();
    player.connect(filter);
    Tone.loaded().then(() => {
      player.start();
    });
  }, []);

  // 14
  const handleKick2Click = useCallback(() => {
    const player = new Tone.Player("/samples/909/BT0A0A7.WAV").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    const crusher = new Tone.BitCrusher(4).toDestination();
    player.connect(distortion);
    const filter = new Tone.Filter(400, "lowpass").toDestination();
    player.connect(filter);
    Tone.loaded().then(() => {
      player.start();
    });
  }, []);

  // 15
  const handleKick3Click = useCallback(() => {
    const player = new Tone.Player("/samples/808bd/BD0025.WAV").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    const crusher = new Tone.BitCrusher(4).toDestination();
    player.connect(distortion);
    const filter = new Tone.Filter(400, "lowpass").toDestination();
    player.connect(filter);
    Tone.loaded().then(() => {
      player.start();
    });
  }, []);

  // 16
  const handleKick4Click = useCallback(() => {
    const player = new Tone.Player("/samples/808bd/BD0010.WAV").toDestination();
    const distortion = new Tone.Distortion(0.4).toDestination();
    const crusher = new Tone.BitCrusher(4).toDestination();
    player.connect(distortion);
    const filter = new Tone.Filter(400, "lowpass").toDestination();
    player.connect(filter);
    Tone.loaded().then(() => {
      player.start();
    });
  }, []);

  return (
    <div className={styles.Pads}>
      <Pad clickPad={handleS1Click} />
      <Pad clickPad={handleS2Click} />
      <Pad clickPad={handleS3Click} />
      <Pad clickPad={handleS4Click} />

      <Pad clickPad={handleHHClick} />
      <Pad clickPad={handleHH2Click} />
      <Pad clickPad={handleHH3Click} />
      <Pad clickPad={handleHH4Click} />

      <Pad clickPad={handleSnareClick} />
      <Pad clickPad={handleSnare2Click} />
      <Pad clickPad={handleSnare3Click} />
      <Pad clickPad={handleSnare4Click} />

      <Pad clickPad={handleKickClick} />
      <Pad clickPad={handleKick2Click} />
      <Pad clickPad={handleKick3Click} />
      <Pad clickPad={handleKick4Click} />

      {/* <Pad clickPad={handleCLClick} /> */}
      {/* <button onClick={handleClockClick}>clock</button> */}
    </div>
  );
};

export default Music;

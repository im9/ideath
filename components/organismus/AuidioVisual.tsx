import React, { useState } from "react";
import { useRef, useEffect } from "react";
import WaveSurfer from "wavesurfer.js";

type Props = {
  url?: String;
};

const AuidioVisual: React.FC<Props> = ({ url }) => {
  const [isInit, setIsInit] = useState<boolean>(false);
  const waveformRef = useRef<any>(null);

  useEffect(() => {
    if (url && !isInit) {
      if (!isInit) {
        waveformRef.current = WaveSurfer.create({
          container: waveformRef.current,
        });
        setIsInit(true);
      }
      waveformRef?.current?.load(url);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [url]);

  return <div ref={waveformRef}></div>;
};

export default AuidioVisual;

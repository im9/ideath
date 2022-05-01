import React, { useState, useEffect } from "react";
import Image from "next/image";
import { motion, useAnimation } from "framer-motion";

const animationVariants = {
  visible: { opacity: 1 },
  hidden: { opacity: 0 },
};

const FadeInImage = (props: any) => {
  const [loaded, setLoaded] = useState(false);
  const animationControls = useAnimation();

  useEffect(
    () => {
      if (loaded) {
        animationControls.start("visible");
      }
    },
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [loaded]
  );

  const alt = props?.alt || "";

  return (
    <motion.div
      initial={"hidden"}
      animate={{
        ...animationControls,
      }}
      variants={animationVariants}
    >
      <Image alt={alt} {...props} onLoadingComplete={() => setLoaded(true)} />
    </motion.div>
  );
};

export default FadeInImage;

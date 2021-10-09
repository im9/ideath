import type { AppProps } from "next/app";
import { Provider } from "@/contexts/state";
import "../styles/globals.css";

function MyApp({ Component, pageProps }: AppProps) {
  return (
    <Provider>
      <Component {...pageProps} />
    </Provider>
  );
}
export default MyApp;

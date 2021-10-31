import type { AppProps } from "next/app";
import { Provider } from "@/contexts/state";
import { RecoilRoot } from "recoil";
import "../styles/globals.css";

function MyApp({ Component, pageProps }: AppProps) {
  return (
    <Provider>
      {/* NOTE: Context を置き換えるか検討 */}
      <RecoilRoot>
        <Component {...pageProps} />
      </RecoilRoot>
    </Provider>
  );
}
export default MyApp;

import type { MsgpackPjsekaiWasmModule } from './msgpack-pjsekai.js';

export interface MsgpackPjsekaiWasmFactoryOptions {
  locateFile?(path: string, prefix: string): string;
  wasmBinary?: ArrayBuffer | Uint8Array;
  [key: string]: unknown;
}

export default function createMsgpackPjsekaiWasm(
  options?: MsgpackPjsekaiWasmFactoryOptions
): Promise<MsgpackPjsekaiWasmModule>;

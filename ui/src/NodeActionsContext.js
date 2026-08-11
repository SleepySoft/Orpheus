import { createContext } from 'react';

/** Context used by React Flow nodes to trigger app-level actions
 *  (e.g., opening the component README modal) without passing callbacks
 *  through the serializable node data. */
export const NodeActionsContext = createContext({
  showReadme: () => {},
});

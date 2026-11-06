import {createRoot} from 'react-dom/client';
import {FoundationProvider} from '@platform/react';
import {AdminApplication} from './app';
import './style.css';

createRoot(document.getElementById('root')!).render(<FoundationProvider><AdminApplication/></FoundationProvider>);

import './App.css'
import '@fontsource/roboto/300.css';
import '@fontsource/roboto/400.css';
import '@fontsource/roboto/500.css';
import '@fontsource/roboto/700.css';

import { initialState, useStateExported, type State } from './state';
import CircularProgress from '@mui/material/CircularProgress';
import Card from '@mui/material/Card';
import Typography from '@mui/material/Typography';
import { Box } from '@mui/system';
import NewAuthCard from './components/NewAuthCard';
import { useState } from 'react';
import LoginCard from './components/LoginCard';

function App() {
  
  let state = useStateExported();

  return (
    <>
      {state.authState.stage === 'fetching_auth' && <CircularProgress />}
      {state.authState.stage === 'connecting' && <CircularProgress />}
      {state.authState.stage === 'login_prompt' && <LoginCard />}
      {state.authState.stage === 'no_auth' && <NewAuthCard />}
      {state.authState.stage === 'error' && <Card>
        <Box sx={{ p: 2, maxWidth: 400, maxHeight: 400, overflow: 'auto' }}>
          <Typography variant="h4" color='error'>Error</Typography>
          <Typography variant="body2" sx={{ color: 'text.secondary' }}>{state.authState.message}</Typography>
        </Box>
      </Card>}
    </>
  )
}

export default App

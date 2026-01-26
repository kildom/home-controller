import { Card, CardContent, Typography, TextField, Button, Box } from '@mui/material';
import { getState, initialState, setState } from '../state';
import { createAuthFile } from '../crypto';
import { memo } from 'react';


function passwordChange(e: React.ChangeEvent<HTMLInputElement>) {
    getState().authState.setPassword(e.target.value).commit();
}

async function generateAuth() {
    let auth = await createAuthFile(getState().authState.password);
    getState().authState.setAuthFile(auth);
}


export default function NewAuthCard() {

    let state = getState();

    return (
        <Card sx={{ maxWidth: 400, mx: 'auto', mt: 4 }}>
            <CardContent>
                <Typography variant="h4" component="h2" gutterBottom>
                    New Auth File
                </Typography>
                <Box sx={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
                    <Typography variant="body2" sx={{ color: 'text.secondary' }}>
                        No authentication file found (HTTP 404 Not Found).
                        Please create new authentication data by entering your password below.
                        Put it later on the server as "<code>auth.json</code>" file.
                    </Typography>
                    <TextField
                        label="Password"
                        variant="outlined"
                        type="password"
                        fullWidth
                        value={state.authState.password}
                        onChange={passwordChange}
                    />
                    {state.authState.authFile &&
                        <TextField
                            id="outlined-multiline-flexible"
                            label="auth.json"
                            multiline
                            maxRows={7}
                            value={state.authState.authFile}
                        />
                    }
                    <Button
                        variant="contained"
                        color="primary"
                        fullWidth
                        onClick={generateAuth}
                    >
                        Create Auth File
                    </Button>
                </Box>
            </CardContent>
        </Card>
    );
}
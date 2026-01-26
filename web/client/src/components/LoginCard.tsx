import { Card, CardContent, Typography, TextField, Button, Box } from '@mui/material';
import { getState, initialState, setState } from '../state';
import { createAuthFile } from '../crypto';
import { memo } from 'react';


function passwordChange(e: React.ChangeEvent<HTMLInputElement>) {
    getState().authState.setPassword(e.target.value).commit();
}

const TextFieldMemo = memo(TextField);

export default function LoginCard() {

    let state = getState();

    return (
        <Card sx={{ maxWidth: 400, mx: 'auto', mt: 4 }}>
            <CardContent>
                <Typography variant="h4" component="h2" gutterBottom>
                    Login
                </Typography>
                <Box sx={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
                    <TextFieldMemo
                        label="Password"
                        variant="outlined"
                        type="password"
                        color={state.authState.passwordValid ? 'primary' : 'error'}
                        fullWidth
                        value={state.authState.password}
                        onChange={passwordChange}
                    />
                    {!state.authState.passwordValid &&
                        <Typography variant="body2" sx={{ color: 'text.secondary' }}>
                            { state.authState.password !== '' ? 'Invalid password.' : 'Type password.' }
                        </Typography>
                    }
                </Box>
            </CardContent>
        </Card>
    );
}

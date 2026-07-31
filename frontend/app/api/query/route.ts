import { NextResponse } from 'next/server';
import { execFile } from 'child_process';
import path from 'path';

export async function POST(request: Request) {
  try {
    const { query } = await request.json();
    if (!query || query.trim() === '') {
      return NextResponse.json({ success: false, error: 'SQL query cannot be empty' }, { status: 400 });
    }

    const exePath = path.join(process.cwd(), '..', 'build', 'dbforge.exe');
    
    return new Promise<Response>((resolve) => {
      execFile(exePath, ['--query', query], (error, stdout, stderr) => {
        if (error) {
          resolve(NextResponse.json({ 
            success: false, 
            error: stderr.trim() || error.message || 'Execution error' 
          }));
          return;
        }

        try {
          const res = JSON.parse(stdout);
          resolve(NextResponse.json(res));
        } catch (parseError) {
          resolve(NextResponse.json({ 
            success: false, 
            error: 'Failed to parse database engine output: ' + stdout 
          }));
        }
      });
    });
  } catch (err: any) {
    return NextResponse.json({ success: false, error: err.message }, { status: 500 });
  }
}

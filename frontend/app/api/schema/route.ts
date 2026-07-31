import { NextResponse } from 'next/server';
import { execFile } from 'child_process';
import path from 'path';

export async function GET() {
  try {
    const exePath = path.join(process.cwd(), '..', 'build', 'dbforge.exe');
    
    return new Promise<Response>((resolve) => {
      execFile(exePath, ['--schema'], (error, stdout, stderr) => {
        if (error) {
          resolve(NextResponse.json({ 
            success: false, 
            error: stderr.trim() || error.message || 'Failed to read schema' 
          }));
          return;
        }

        try {
          const res = JSON.parse(stdout);
          resolve(NextResponse.json(res));
        } catch (parseError) {
          resolve(NextResponse.json({ 
            success: false, 
            error: 'Failed to parse schema output: ' + stdout 
          }));
        }
      });
    });
  } catch (err: any) {
    return NextResponse.json({ success: false, error: err.message }, { status: 500 });
  }
}

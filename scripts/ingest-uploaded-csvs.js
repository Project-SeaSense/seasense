// scripts/ingest-csvs.js
const { createClient } = require('@supabase/supabase-js');
const csvParse = require('csv-parse/sync');
const fs = require('fs');
const path = require('path');

const SUPABASE_URL = process.env.SUPABASE_URL;
const SUPABASE_SERVICE_ROLE_KEY = process.env.SUPABASE_SERVICE_ROLE_KEY;
const BUCKET = 'seasense-raw-file';

const supabase = createClient(SUPABASE_URL, SUPABASE_SERVICE_ROLE_KEY);

async function listCsvFiles() {
  const { data, error } = await supabase.storage.from(BUCKET).list('', { limit: 1000 });
  if (error) throw error;
  return data.filter(f => f.name.endsWith('.csv') && !f.name.endsWith('.log.csv'));
}

async function downloadFile(filename) {
  const { data, error } = await supabase.storage.from(BUCKET).download(filename);
  if (error) throw error;
  return Buffer.from(await data.arrayBuffer()).toString('utf-8');
}

async function uploadLogFile(filename, content) {
  const logName = filename.replace(/\.csv$/, '.log');
  await supabase.storage.from(BUCKET).upload(logName, content, { upsert: true, contentType: 'text/plain' });
}

async function fileExists(filename) {
  const logName = filename.replace(/\.csv$/, '.log');
  const { data, error } = await supabase.storage.from(BUCKET).list('');
  if (error) throw error;
  return data.some(f => f.name === logName);
}

async function insertRecord(row, columns) {
  // Map CSV row to object
  const obj = {};
  columns.forEach((col, i) => obj[col] = row[i]);
  // Insert into seasense_raw
  const { error } = await supabase.from('seasense_raw').insert([obj]);
  return error;
}

async function processFile(filename) {
  if (await fileExists(filename)) {
    console.log(`Skipping ${filename}, already processed.`);
    return;
  }
  console.log(`Processing ${filename}...`);
  const csvText = await downloadFile(filename);
  console.log('Downloaded content (first 200 chars):', csvText.slice(0, 200));
  const records = csvParse.parse(csvText, { skip_empty_lines: true });
  const columns = records[0];
  const dataRows = records.slice(1);

  let ingested = 0;
  let failed = [];
  for (const row of dataRows) {
    const error = await insertRecord(row, columns);
    if (error) {
      failed.push([...row, error.message]);
    } else {
      ingested++;
    }
  }

  // Prepare log
  let log = `${dataRows.length} records counted\n${ingested} records ingested\n`;
  if (failed.length > 0) {
    log += 'failed records:\n';
    log += columns.join(',') + ',reason\n';
    for (const row of failed) {
      log += row.join(',') + '\n';
    }
  }
  await uploadLogFile(filename, log);
  console.log(`Finished ${filename}: ${ingested} ingested, ${failed.length} failed.`);
}

async function main() {
  const files = await listCsvFiles();
  for (const file of files) {
    await processFile(file.name);
  }
}

main().catch(e => {
  console.error(e);
  process.exit(1);
});
